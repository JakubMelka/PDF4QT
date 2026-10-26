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

#include "pdfappsettingspage.h"
#include "pdfappcontext.h"
#include "pdfappinfowidgets.h"
#include "pdfapprestoredefaultsdialog.h"
#include "pdfappsettingrow.h"
#include "pdfappsettingssection.h"
#include "pdfappworkspace.h"
#include "pdfdatabasenotifier.h"
#include "pdfsettingdefinitions.h"

#include "pdfdocumenttoolsearch.h"

#include "pdfaccenthue.h"
#include "pdfcomponentbadges.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"
#include "pdfthemefonts.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

namespace pdf
{

PDFAppSettingsPage::PendingFocus PDFAppSettingsPage::s_pendingFocus = PDFAppSettingsPage::PendingFocus::None;

PDFAppSettingsSectionHeader::PDFAppSettingsSectionHeader(const QString& iconName, const QString& title, const QString& subtitle, bool hasRestoreButton, QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("sectionHeader"));
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    // icon-circle 146:3330: 44, the icon 22, accent/blue
    m_iconBadge = new PDFIconBadge(iconName, 44, 22, this);
    m_iconBadge->setObjectName(QStringLiteral("sectionIcon"));
    m_iconBadge->setHue(PDFAccentHue::Blue);
    layout->addWidget(m_iconBadge, 0, Qt::AlignVCenter);

    // text 146:3333: the title Bold 22 and the subtitle Regular 13, gap 2
    QWidget* texts = new QWidget(this);
    QVBoxLayout* textsLayout = PDFAppPageWidgets::createColumn(texts, 2);
    m_title = PDFAppPageWidgets::createText(title, PDFThemeFonts::getFont(QFont::Bold, 22), 27, PDFThemeColors::TextPrimary, texts);
    m_title->setObjectName(QStringLiteral("sectionTitle"));
    textsLayout->addWidget(m_title);
    m_subtitle = PDFAppPageWidgets::createText(subtitle, PDFThemeFonts::getFont(PDFThemeFonts::Role::Body), 16, PDFThemeColors::TextTertiary, texts);
    m_subtitle->setObjectName(QStringLiteral("sectionSubtitle"));
    m_subtitle->setHidden(subtitle.isEmpty());
    textsLayout->addWidget(m_subtitle);
    layout->addWidget(texts, 1, Qt::AlignVCenter);

    if (hasRestoreButton)
    {
        // restore-defaults 146:3336: Secondary with the icon rotate-ccw
        m_restoreButton = PDFAppPageWidgets::createButton(tr("Restore defaults"), QStringLiteral("rotate-ccw"), false, this);
        m_restoreButton->setObjectName(QStringLiteral("restoreDefaults"));
        layout->addWidget(m_restoreButton, 0, Qt::AlignVCenter);
    }
}

PDFAppSettingsSectionHeader::~PDFAppSettingsSectionHeader() = default;

PDFAppSettingsPage::PDFAppSettingsPage(PDFAppContext* context, QWidget* parent) :
    BaseClass(context, parent),
    m_focus(s_pendingFocus)
{
    // The focus asked for by the page, which navigated here, is taken once
    s_pendingFocus = PendingFocus::None;
    setObjectName(QStringLiteral("settingsPage"));

    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(this, 0);

    // settings-grid 146:2884: the menu 300 and the content column, gap 24; a narrow
    // page has the menu above the content
    m_columns = new PDFAppColumns(this);
    m_columns->setObjectName(QStringLiteral("settingsGrid"));
    layout->addWidget(m_columns);

    m_menuColumn = new QWidget();
    m_menuColumn->setObjectName(QStringLiteral("settingsMenu"));
    PDFAppPageWidgets::createColumn(m_menuColumn, 0);
    m_columns->addColumn(m_menuColumn, MENU_WIDTH, 0, 0, Qt::AlignTop);

    // content-column 146:3131: the storage banner (941:138200) and the body, gap 24
    m_contentColumn = new QWidget();
    m_contentColumn->setObjectName(QStringLiteral("contentColumn"));
    m_contentLayout = PDFAppPageWidgets::createColumn(m_contentColumn, 24);
    m_columns->addColumn(m_contentColumn, 0, 1, MINIMUM_CONTENT_WIDTH, Qt::AlignTop);
    layout->addStretch(1);

    m_searchTimer.setSingleShot(true);
    m_searchTimer.setInterval(SEARCH_DELAY);
    connect(&m_searchTimer, &QTimer::timeout, this, &PDFAppSettingsPage::onSearchTimeout);
    m_statusTimer.setSingleShot(true);
    m_statusTimer.setInterval(STATUS_DURATION);
    connect(&m_statusTimer, &QTimer::timeout, this, &PDFAppSettingsPage::onStatusTimeout);

    buildMenu();
    buildContent();

    if (context && context->getNotifier())
    {
        connect(context->getNotifier(), &PDFDatabaseNotifier::changed, this, &PDFAppSettingsPage::onDatabaseChanged);
        connect(context->getNotifier(), &PDFDatabaseNotifier::everythingChanged, this, &PDFAppSettingsPage::onEverythingChanged);
    }
}

PDFAppSettingsPage::~PDFAppSettingsPage()
{
    // Nothing comes back to the page, which is being destroyed: the notifications,
    // the dialog and the sections are disconnected. The pending values are written
    // by the destructors of their binders and sections without notifications
    // (~PDFAppSettingControl, the sections Color modes and Read aloud): a
    // notified write here would run the slots of the sections (a new engine of
    // the speech) from the destructor of the page.
    if (PDFAppContext* context = getContext(); context && context->getNotifier())
    {
        context->getNotifier()->disconnect(this);
    }

    if (PDFAppRestoreDefaultsDialog* dialog = m_restoreDialog.data())
    {
        dialog->disconnect(this);
        dialog->hide();
        dialog->deleteLater();
    }

    for (const QPointer<PDFAppSettingsSection>& section : std::as_const(m_sections))
    {
        if (section)
        {
            section->disconnect(this);
        }
    }
}

// ------------------------------------------------------------------ building

void PDFAppSettingsPage::buildMenu()
{
    m_menuBody = new QWidget(m_menuColumn);
    m_menuBody->setObjectName(QStringLiteral("menuBody"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(m_menuBody, 16);

    // title-group 146:3067: Settings Bold 28 (Inter Extra Bold is not bundled) and
    // the subtitle Regular 14 text/tertiary, gap 4
    QWidget* titleGroup = new QWidget(m_menuBody);
    QVBoxLayout* titleLayout = PDFAppPageWidgets::createColumn(titleGroup, 4);
    PDFAppParagraph* title = PDFAppPageWidgets::createText(tr("Settings"), PDFThemeFonts::getFont(QFont::Bold, 28), 34, PDFThemeColors::TextPrimary, titleGroup);
    title->setObjectName(QStringLiteral("menuTitle"));
    titleLayout->addWidget(title);
    PDFAppParagraph* subtitle = PDFAppPageWidgets::createText(tr("Adjust PDF4QT to the way you work."), PDFThemeFonts::getFont(QFont::Normal, 14), 17, PDFThemeColors::TextTertiary, titleGroup);
    subtitle->setObjectName(QStringLiteral("menuSubtitle"));
    titleLayout->addWidget(subtitle);
    layout->addWidget(titleGroup);

    // settings-search 146:3070
    m_searchField = new PDFLineEdit(m_menuBody);
    m_searchField->setObjectName(QStringLiteral("settingsSearch"));
    m_searchField->setSearchField();
    m_searchField->setPlaceholderText(tr("Search settings…"));
    m_searchField->setAccessibleName(tr("Search settings"));
    m_searchField->setMaxLength(MAXIMUM_QUERY_LENGTH);
    m_searchField->setClearButtonEnabled(true);
    m_searchField->installEventFilter(this);
    connect(m_searchField.data(), &QLineEdit::textEdited, this, &PDFAppSettingsPage::onSearchTextEdited);
    layout->addWidget(m_searchField);

    // sections 146:3074: a card (padding 8) with the Settings nav items, gap 2
    PDFAppCard* card = PDFAppPageWidgets::createCard(8, 0, m_menuBody);
    card->setObjectName(QStringLiteral("sections"));
    m_navigation = new PDFNavList(PDFNavItem::Kind::Settings, card);
    m_navigation->setObjectName(QStringLiteral("settingsNavigation"));
    m_navigation->setAccessibleName(tr("Sections of the settings"));
    for (const PDFAppSettingsSectionInfo& info : PDFAppSettingsSectionRegistry::getSections())
    {
        const int index = m_navigation->addItem(info.getTitle(), info.iconName, info.getNavigationSubtitle());
        if (PDFNavItem* item = m_navigation->getItem(index))
        {
            item->setObjectName(QStringLiteral("nav-%1").arg(info.id));
        }
    }
    card->getLayout()->addWidget(m_navigation);
    connect(m_navigation.data(), &PDFNavList::currentIndexChanged, this, &PDFAppSettingsPage::onNavigationIndexChanged);
    layout->addWidget(card);

    if (QBoxLayout* columnLayout = qobject_cast<QBoxLayout*>(m_menuColumn->layout()))
    {
        columnLayout->addWidget(m_menuBody);
    }
}

void PDFAppSettingsPage::buildContent()
{
    m_contentBody = new QWidget(m_contentColumn);
    m_contentBody->setObjectName(QStringLiteral("contentBody"));
    m_bodyLayout = PDFAppPageWidgets::createColumn(m_contentBody, 24);

    // A refused write (a busy or read-only database)
    m_statusLabel = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(PDFThemeFonts::Role::Small), 15, PDFThemeColors::TextDanger, m_contentBody);
    m_statusLabel->setObjectName(QStringLiteral("statusMessage"));
    m_statusLabel->hide();
    m_bodyLayout->addWidget(m_statusLabel);

    // autosave-note 146:3411: the icon info 14 and the text Regular 12 text/muted,
    // gap 8; the portable and memory modes keep nothing (941:138326)
    QWidget* footer = new QWidget(m_contentBody);
    footer->setObjectName(QStringLiteral("autosaveNote"));
    QHBoxLayout* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->setSpacing(8);
    PDFIconBadge* icon = new PDFIconBadge(QStringLiteral("info"), 14, 14, footer);
    icon->setColors(std::nullopt, PDFThemeColors::IconMuted);
    footerLayout->addWidget(icon, 0, Qt::AlignVCenter);

    bool isPersistent = true;
    if (PDFAppContext* context = getContext())
    {
        isPersistent = context->getStorageState().mode == PDFDatabaseStorageMode::Disk;
    }
    const QString note = isPersistent ? tr("Changes are saved automatically.") : tr("Changes apply at once and are kept until PDF4QT closes.");
    m_footerNote = PDFAppPageWidgets::createText(note, PDFThemeFonts::getFont(PDFThemeFonts::Role::Small), 15, PDFThemeColors::TextMuted, footer);
    m_footerNote->setObjectName(QStringLiteral("footerNote"));
    footerLayout->addWidget(m_footerNote, 1);
    m_bodyLayout->addWidget(footer);

    if (m_contentLayout)
    {
        m_contentLayout->addWidget(m_contentBody);
    }
}

void PDFAppSettingsPage::rebuild()
{
    // The language has changed: the texts are created again, the route, the text of
    // the search, the scroll position and the focused widget are kept
    const int scrollPosition = isVisible() ? getScrollPosition() : -1;
    const QString focusName = PDFAppPageRebuild::getFocusedName(this);
    const QString searchText = m_searchField ? m_searchField->text() : QString();
    const int cursorPosition = m_searchField ? m_searchField->cursorPosition() : 0;

    // The slots of a written value may delete the page
    QPointer<PDFAppSettingsPage> guard(this);
    commitPendingValues();
    if (!guard)
    {
        return;
    }
    clearContent();
    delete m_menuBody.data();
    delete m_contentBody.data();
    buildMenu();
    buildContent();

    if (isFiltering())
    {
        showFilter(m_query);
    }
    else
    {
        showSection(m_sectionId, QString());
    }

    if (m_searchField)
    {
        m_searchField->setText(searchText);
        m_searchField->setCursorPosition(cursorPosition);
    }

    if (scrollPosition >= 0)
    {
        PDFAppPageRebuild::finishLayout();
        setScrollPosition(scrollPosition);
        requestDeferredScroll(scrollPosition, QString());
    }
    PDFAppPageRebuild::restoreFocus(this, focusName);
}

void PDFAppSettingsPage::clearContent()
{
    // The dialog belongs to the section, which goes. It is hidden at once and
    // deleted later: the page is built again while QEvent::LanguageChange is sent
    // to a copy of the list of the children of the window, of which the dialog is
    // one (its scrim is deleted later by the box).
    if (PDFAppRestoreDefaultsDialog* dialog = m_restoreDialog.data())
    {
        dialog->disconnect(this);
        dialog->hide();
        dialog->deleteLater();
    }
    m_restoreDialog = nullptr;

    for (const QPointer<PDFAppSettingsSection>& section : std::as_const(m_sections))
    {
        delete section.data();
    }
    m_sections.clear();
    for (const auto& item : m_filterHeaders)
    {
        delete item.second.data();
    }
    m_filterHeaders.clear();
    delete m_sectionHeader.data();
    delete m_emptyState.data();
}

void PDFAppSettingsPage::connectSection(PDFAppSettingsSection* section)
{
    connect(section, &PDFAppSettingsSection::statusMessageRequested, this, &PDFAppSettingsPage::onStatusMessageRequested);
    connect(section, &PDFAppSettingsSection::navigationRequested, this, &PDFAppSettingsPage::onSectionNavigationRequested);
}

void PDFAppSettingsPage::showSection(const QString& sectionId, const QString& anchor)
{
    const PDFAppSettingsSectionInfo* info = PDFAppSettingsSectionRegistry::findSection(sectionId);
    if (!info || !m_bodyLayout || !m_contentBody)
    {
        return;
    }

    PDFAppSettingsSection* section = PDFAppSettingsSectionRegistry::createSection(getContext(), sectionId, m_contentBody);
    if (!section)
    {
        return;
    }

    m_sectionHeader = new PDFAppSettingsSectionHeader(section->getIconName(), section->getTitle(), section->getSubtitle(), section->isRestoreSupported(), m_contentBody);
    if (QPushButton* button = m_sectionHeader->getRestoreButton())
    {
        connect(button, &QPushButton::clicked, this, &PDFAppSettingsPage::onRestoreClicked);
    }

    const int index = m_bodyLayout->indexOf(m_statusLabel);
    m_bodyLayout->insertWidget(index, m_sectionHeader);
    m_bodyLayout->insertWidget(index + 1, section);
    connectSection(section);
    m_sections.append(section);

    updateRestoreButton();
    updateNavigation();

    // The scroll is done, when the page is shown and laid out
    if (QWidget* widget = findAnchorTarget(anchor))
    {
        setAnchorWidget(widget);
        highlightAnchor(widget);
    }
}

QWidget* PDFAppSettingsPage::findAnchorTarget(const QString& anchor) const
{
    if (anchor.isEmpty())
    {
        return nullptr;
    }

    // The rows and the custom parts of the sections (the table of the certificates,
    // a command of Shortcuts); a part hidden by the filter is not a target
    for (PDFAppSettingsSection* section : getSections())
    {
        QWidget* widget = section->findAnchorWidget(anchor);
        bool isFilteredOut = false;
        for (QWidget* current = widget; current && current != this; current = current->parentWidget())
        {
            // Explicitly hidden (a new widget of a shown page is hidden until the
            // event loop shows it)
            isFilteredOut = isFilteredOut || (current->isHidden() && current->testAttribute(Qt::WA_WState_ExplicitShowHide));
        }
        if (widget && !isFilteredOut)
        {
            return widget;
        }
    }
    return nullptr;
}

void PDFAppSettingsPage::highlightAnchor(QWidget* widget)
{
    // The row of the anchor (also a block row of a package, or the row of a widget
    // of a section) is highlighted; another custom part (a table) is only scrolled
    // to, it shows its own current item
    for (QWidget* current = widget; current && current != this; current = current->parentWidget())
    {
        if (PDFAppSettingRow* row = qobject_cast<PDFAppSettingRow*>(current))
        {
            row->highlight();
            return;
        }
    }
}

void PDFAppSettingsPage::showFilter(const QString& query)
{
    if (!m_bodyLayout || !m_contentBody)
    {
        return;
    }

    // Every section below its own header (without Restore defaults); the filter
    // shows the matching rows, a section without a match is hidden
    int index = m_bodyLayout->indexOf(m_statusLabel);
    for (const PDFAppSettingsSectionInfo& info : PDFAppSettingsSectionRegistry::getSections())
    {
        PDFAppSettingsSection* section = PDFAppSettingsSectionRegistry::createSection(getContext(), info.id, m_contentBody);
        if (!section)
        {
            continue;
        }

        PDFAppSettingsSectionHeader* header = new PDFAppSettingsSectionHeader(section->getIconName(), section->getTitle(), section->getSubtitle(), false, m_contentBody);
        header->setObjectName(QStringLiteral("sectionHeader-%1").arg(info.id));
        m_bodyLayout->insertWidget(index++, header);
        m_bodyLayout->insertWidget(index++, section);
        connectSection(section);
        m_sections.append(section);
        m_filterHeaders[info.id] = header;
    }

    // No match (no Figma frame): a card with the text and Clear search
    PDFAppCard* emptyState = PDFAppPageWidgets::createCard(24, 12, m_contentBody);
    emptyState->setObjectName(QStringLiteral("emptyState"));
    m_emptyText = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(PDFThemeFonts::Role::Body), 20, PDFThemeColors::TextTertiary, emptyState);
    m_emptyText->setObjectName(QStringLiteral("emptyText"));
    m_emptyText->setAlignment(Qt::AlignHCenter);
    emptyState->getLayout()->addWidget(m_emptyText);
    m_clearSearchButton = PDFAppPageWidgets::createButton(tr("Clear search"), QStringLiteral("x"), false, emptyState);
    m_clearSearchButton->setObjectName(QStringLiteral("clearSearch"));
    connect(m_clearSearchButton.data(), &QPushButton::clicked, this, &PDFAppSettingsPage::onClearSearchClicked);
    emptyState->getLayout()->addWidget(m_clearSearchButton, 0, Qt::AlignHCenter);
    m_bodyLayout->insertWidget(index, emptyState);
    m_emptyState = emptyState;

    m_query = query;
    updateFilter();
}

void PDFAppSettingsPage::updateFilter()
{
    const PDFSearchQuery query(m_query);
    int total = 0;
    std::map<QString, int> counts;
    for (const QPointer<PDFAppSettingsSection>& section : std::as_const(m_sections))
    {
        if (!section)
        {
            continue;
        }

        const int count = section->applyFilter(query);
        counts[section->getSectionId()] = count;
        total += count;
        section->setHidden(count == 0);
        auto it = m_filterHeaders.find(section->getSectionId());
        if (it != m_filterHeaders.end() && it->second)
        {
            it->second->setHidden(count == 0);
        }
    }

    if (m_emptyState)
    {
        m_emptyState->setHidden(total > 0);
    }
    if (m_emptyText)
    {
        m_emptyText->setText(tr("No settings match “%1”").arg(PDFSearchQuery::sanitize(m_query, 64)));
    }

    // The navigation shows the sections with a match and their counts
    if (m_navigation)
    {
        const std::vector<PDFAppSettingsSectionInfo>& sections = PDFAppSettingsSectionRegistry::getSections();
        for (int i = 0; i < int(sections.size()); ++i)
        {
            if (PDFNavItem* item = m_navigation->getItem(i))
            {
                const int count = counts.count(sections[size_t(i)].id) ? counts[sections[size_t(i)].id] : 0;
                item->setVisible(count > 0);
                item->setSubtitle(count == 1 ? tr("%Ln match", nullptr, count) : tr("%Ln matches", nullptr, count));
            }
        }
    }
    updateNavigation();
}

void PDFAppSettingsPage::updateNavigation()
{
    if (!m_navigation)
    {
        return;
    }

    // The section of the page is the current item; the filtered page has none
    const int index = isFiltering() ? -1 : int(PDFAppSettingsSectionRegistry::getSectionIds().indexOf(m_sectionId));
    if (m_navigation->getCurrentIndex() != index)
    {
        m_isUpdatingNavigation = true;
        QPointer<PDFAppSettingsPage> guard(this);
        m_navigation->setCurrentIndex(index);
        if (guard)
        {
            m_isUpdatingNavigation = false;
        }
    }
}

void PDFAppSettingsPage::updateRestoreButton()
{
    QPushButton* button = getRestoreButton();
    PDFAppSettingsSection* section = getSection();
    if (!button || !section)
    {
        return;
    }

    button->setVisible(section->isRestoreSupported());
    button->setEnabled(section->isRestoreSupported() && !section->getChangedKeys().isEmpty());
}

// ------------------------------------------------------------------ route

bool PDFAppSettingsPage::setRoute(const PDFAppRoute& route)
{
    const QStringList& segments = route.getSegments();
    if (segments.isEmpty() || segments.front() != QLatin1String("settings") || segments.size() > 2)
    {
        return false;
    }

    QString sectionId;
    QString query;
    if (segments.size() == 2)
    {
        sectionId = segments[1];
        if (!PDFAppSettingsSectionRegistry::findSection(sectionId) || !route.getQueryItems().isEmpty())
        {
            return false;
        }
    }
    else
    {
        // /settings?q=: an empty filter shows General
        query = sanitizeQuery(route.getQueryValue(QStringLiteral("q")));
        if (query.trimmed().isEmpty())
        {
            query.clear();
            sectionId = PDFSettingDefinitions::sectionGeneral();
        }
    }

    // The slots of a written value may delete the page
    QPointer<PDFAppSettingsPage> guard(this);
    commitPendingValues();
    if (!guard)
    {
        return false;
    }
    clearContent();
    m_sectionId = sectionId;
    m_query = query;
    m_anchor = route.getFragment();

    if (m_searchField && !query.isEmpty() && sanitizeQuery(m_searchField->text()) != query)
    {
        m_searchField->setText(route.getQueryValue(QStringLiteral("q")).left(MAXIMUM_QUERY_LENGTH));
    }

    if (isFiltering())
    {
        showFilter(query);
    }
    else
    {
        showSection(sectionId, m_anchor);
    }

    return BaseClass::setRoute(route);
}

QString PDFAppSettingsPage::getTitle() const
{
    return tr("Settings");
}

bool PDFAppSettingsPage::hasSearchHeader() const
{
    // 146:2843: the page has its own title and search (no header of the workspace)
    return false;
}

QBoxLayout* PDFAppSettingsPage::getBannerLayout() const
{
    // The storage banner above the section (941:138200)
    return m_contentLayout;
}

PDFNavItem* PDFAppSettingsPage::getNavigationItem(const QString& sectionId) const
{
    const int index = int(PDFAppSettingsSectionRegistry::getSectionIds().indexOf(sectionId));
    return m_navigation && index >= 0 ? m_navigation->getItem(index) : nullptr;
}

QPushButton* PDFAppSettingsPage::getRestoreButton() const
{
    return m_sectionHeader ? m_sectionHeader->getRestoreButton() : nullptr;
}

PDFAppSettingsSection* PDFAppSettingsPage::getSection() const
{
    if (isFiltering() || m_sections.isEmpty())
    {
        return nullptr;
    }
    return m_sections.front().data();
}

QList<PDFAppSettingsSection*> PDFAppSettingsPage::getSections() const
{
    QList<PDFAppSettingsSection*> sections;
    for (const QPointer<PDFAppSettingsSection>& section : m_sections)
    {
        if (section)
        {
            sections.append(section.data());
        }
    }
    return sections;
}

PDFAppSettingRow* PDFAppSettingsPage::findRow(const QString& anchor) const
{
    for (PDFAppSettingsSection* section : getSections())
    {
        if (PDFAppSettingRow* row = section->findRow(anchor))
        {
            return row;
        }
    }
    return nullptr;
}

QString PDFAppSettingsPage::sanitizeQuery(const QString& text)
{
    // The text of the field is untrusted (pasted): the text of a valid route
    return PDFAppRoutes::settingsFilter(text.left(MAXIMUM_QUERY_LENGTH * 4)).getQueryValue(QStringLiteral("q"));
}

PDFAppRoute PDFAppSettingsPage::createFilterRoute(const QString& text)
{
    return PDFAppRoutes::settingsFilter(sanitizeQuery(text));
}

bool PDFAppSettingsPage::scrollToAnchor(const QString& anchor)
{
    // The same targets as the anchor of a route (showSection)
    QWidget* widget = findAnchorTarget(anchor);
    if (!widget)
    {
        return false;
    }

    scrollToWidget(widget);
    highlightAnchor(widget);
    return true;
}

// ------------------------------------------------------------------ actions

void PDFAppSettingsPage::showStatusMessage(const QString& message)
{
    if (!m_statusLabel)
    {
        return;
    }

    m_statusLabel->setText(message);
    m_statusLabel->setHidden(message.isEmpty());
    m_statusTimer.start();
}

bool PDFAppSettingsPage::openRestoreDefaultsDialog()
{
    PDFAppSettingsSection* section = getSection();
    if (!section || !section->isRestoreSupported())
    {
        return false;
    }

    if (m_restoreDialog)
    {
        m_restoreDialog->raise();
        return true;
    }

    // A value being edited is a changed value too
    QPointer<PDFAppSettingsPage> guard(this);
    section->commitPendingValues();
    if (!guard || section->getChangedKeys().isEmpty())
    {
        return false;
    }

    PDFAppRestoreDefaultsDialog* dialog = new PDFAppRestoreDefaultsDialog(section, window());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::finished, this, &PDFAppSettingsPage::onRestoreDialogFinished);
    m_restoreDialog = dialog;
    dialog->open();
    return true;
}

void PDFAppSettingsPage::commitPendingValues()
{
    // A copy: a slot of a written value may delete the page with its sections
    const QList<QPointer<PDFAppSettingsSection>> sections = m_sections;
    QPointer<PDFAppSettingsPage> guard(this);
    for (const QPointer<PDFAppSettingsSection>& section : sections)
    {
        if (!guard)
        {
            return;
        }
        if (section)
        {
            section->commitPendingValues();
        }
    }
}

bool PDFAppSettingsPage::navigateTo(const PDFAppRoute& route, PDFAppNavigationMode mode, PendingFocus focus)
{
    // The next page of the navigation takes the focus (the page is replaced by it)
    s_pendingFocus = focus;
    const bool isAlive = requestNavigation(route, mode);
    s_pendingFocus = PendingFocus::None;
    return isAlive;
}

bool PDFAppSettingsPage::clearFilter()
{
    // The sections replace the filtered page; when the previous entry of the history
    // is the same page of the sections (the page before the search), the tab goes
    // Back to it - no two equal entries (O25). The next page takes the focus of the field.
    const PDFAppRoute route = PDFAppRoutes::settings();
    if (!isPreviousRoute(route))
    {
        return navigateTo(route, PDFAppNavigationMode::Replace, PendingFocus::Search);
    }

    s_pendingFocus = PendingFocus::Search;
    const bool isAlive = requestBack();
    s_pendingFocus = PendingFocus::None;
    return isAlive;
}

void PDFAppSettingsPage::applyPendingFocus()
{
    const PendingFocus focus = m_focus;
    m_focus = PendingFocus::None;

    if (focus == PendingFocus::Search && m_searchField)
    {
        m_searchField->setFocus(Qt::OtherFocusReason);
        m_searchField->setCursorPosition(int(m_searchField->text().size()));
    }
    else if (focus == PendingFocus::Navigation && m_navigation)
    {
        if (PDFNavItem* item = m_navigation->getItem(m_navigation->getCurrentIndex()))
        {
            item->setFocus(Qt::TabFocusReason);
        }
    }
}

// ------------------------------------------------------------------ events

void PDFAppSettingsPage::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange && m_contentBody)
    {
        rebuild();
    }
}

void PDFAppSettingsPage::showEvent(QShowEvent* event)
{
    BaseClass::showEvent(event);
    if (m_focus != PendingFocus::None)
    {
        QMetaObject::invokeMethod(this, &PDFAppSettingsPage::applyPendingFocus, Qt::QueuedConnection);
    }
}

void PDFAppSettingsPage::hideEvent(QHideEvent* event)
{
    // A value being edited is written, when the page is left - from the event
    // loop: the hide event comes from inside of QWidgetPrivate::hideChildren,
    // which does not survive a slot of the written value (valueWritten, the
    // notifier), which deletes or rebuilds the widgets of the page (the language).
    // A page deleted meanwhile writes the values by its binders without the
    // notifications (~PDFAppSettingControl).
    BaseClass::hideEvent(event);
    QMetaObject::invokeMethod(this, &PDFAppSettingsPage::commitPendingValues, Qt::QueuedConnection);
}

bool PDFAppSettingsPage::event(QEvent* event)
{
    if (event->type() == QEvent::DeferredDelete)
    {
        // The deferred deletion (a page replaced by a navigation) can come before
        // the queued commit of the hide: the pending values are written and the
        // activities of the sections (the sample of the speech) end here, from the
        // event loop, not from the destructor, whose signals would reach code,
        // which deletes an ancestor of the page being destroyed. A slot may
        // delete the page meanwhile (QObject::event deletes it here too).
        QPointer<PDFAppSettingsPage> guard(this);
        commitPendingValues();
        if (!guard)
        {
            return true;
        }
        const QList<QPointer<PDFAppSettingsSection>> sections = m_sections;
        for (const QPointer<PDFAppSettingsSection>& section : sections)
        {
            if (!guard)
            {
                return true;
            }
            if (section)
            {
                section->finishActivity();
            }
        }
        if (!guard)
        {
            return true;
        }
    }
    return BaseClass::event(event);
}

bool PDFAppSettingsPage::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_searchField.data() && event->type() == QEvent::KeyPress)
    {
        const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape && keyEvent->modifiers() == Qt::NoModifier && (isFiltering() || !m_searchField->text().isEmpty()))
        {
            // Esc clears the search and goes back to the sections
            m_searchField->clear();
            m_searchTimer.stop();
            onSearchTimeout();
            return true;
        }
    }
    return BaseClass::eventFilter(watched, event);
}

// ------------------------------------------------------------------ slots

void PDFAppSettingsPage::onNavigationIndexChanged(int index)
{
    if (m_isUpdatingNavigation)
    {
        return;
    }

    const QStringList ids = PDFAppSettingsSectionRegistry::getSectionIds();
    if (index < 0 || index >= ids.size() || (!isFiltering() && ids[index] == m_sectionId))
    {
        return;
    }

    // The arrows of the navigation keep the focus in the navigation of the new page
    const QWidget* focusWidget = QApplication::focusWidget();
    const bool hasFocus = m_navigation && focusWidget && m_navigation->isAncestorOf(focusWidget);
    navigateTo(PDFAppRoutes::settings(ids[index]), PDFAppNavigationMode::Push, hasFocus ? PendingFocus::Navigation : PendingFocus::None);
}

void PDFAppSettingsPage::onSearchTextEdited(const QString& text)
{
    Q_UNUSED(text);
    m_searchTimer.start();
}

void PDFAppSettingsPage::onSearchTimeout()
{
    if (!m_searchField)
    {
        return;
    }

    const QString query = sanitizeQuery(m_searchField->text());
    if (query.trimmed().isEmpty())
    {
        // An emptied filter goes back to the sections
        if (isFiltering())
        {
            clearFilter();
        }
        return;
    }

    if (!isFiltering())
    {
        // The first text: the filtered page (one entry of the history)
        navigateTo(createFilterRoute(query), PDFAppNavigationMode::Push, PendingFocus::Search);
        return;
    }

    if (query != m_query)
    {
        // Further typing replaces the route of the filtered page
        m_query = query;
        updateFilter();
        replaceRoute(createFilterRoute(query));
    }
}

void PDFAppSettingsPage::onClearSearchClicked()
{
    if (m_searchField)
    {
        m_searchField->clear();
    }
    m_searchTimer.stop();
    clearFilter();
}

void PDFAppSettingsPage::onRestoreClicked()
{
    openRestoreDefaultsDialog();
}

void PDFAppSettingsPage::onRestoreDialogFinished(int result)
{
    PDFAppSettingsSection* section = getSection();
    if (result != QDialog::Accepted || !section)
    {
        return;
    }

    // The notifications of the restored values run the code of the application
    QPointer<PDFAppSettingsPage> guard(this);
    const PDFDatabaseError error = section->restoreDefaults();
    if (guard && error.isError())
    {
        showStatusMessage(tr("The defaults could not be restored: %1").arg(error.toString()));
    }
}

void PDFAppSettingsPage::onDatabaseChanged(const QString& topic, const QString& key)
{
    // A section may run the code of the application (a status message, a dialog)
    QPointer<PDFAppSettingsPage> guard(this);
    for (const QPointer<PDFAppSettingsSection>& section : QList<QPointer<PDFAppSettingsSection>>(m_sections))
    {
        if (section)
        {
            section->refresh(topic, key);
        }
        if (!guard)
        {
            return;
        }
    }

    // The changed values are read again only for the topics of the section (Recent,
    // the tool history and the thumbnails write often)
    PDFAppSettingsSection* section = getSection();
    if (section && section->isRestoreTopic(topic))
    {
        updateRestoreButton();
    }
}

void PDFAppSettingsPage::onEverythingChanged()
{
    onDatabaseChanged(QString(), QString());
}

void PDFAppSettingsPage::onStatusMessageRequested(const QString& message)
{
    showStatusMessage(message);
}

void PDFAppSettingsPage::onSectionNavigationRequested(const PDFAppRoute& route)
{
    navigateTo(route, PDFAppNavigationMode::Push, PendingFocus::None);
}

void PDFAppSettingsPage::onStatusTimeout()
{
    if (m_statusLabel)
    {
        m_statusLabel->hide();
    }
}

PDFAppPage* PDFAppSettingsPageFactory::createPage(PDFAppContext* context, QWidget* parent) const
{
    return new PDFAppSettingsPage(context, parent);
}

}   // namespace pdf
