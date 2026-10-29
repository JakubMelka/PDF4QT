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

#include "pdfappdocumenttoolspanel.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentview.h"
#include "pdfapphomewidgets.h"
#include "pdfappmainwindow.h"
#include "pdfappminuteticker.h"
#include "pdfappnavigator.h"
#include "pdfapppagelayout.h"
#include "pdfappsearchwidgets.h"
#include "pdfapptoolpresentation.h"
#include "pdfapptoolstate.h"
#include "pdfdatabase.h"
#include "pdfdatabasenotifier.h"
#include "pdfdatabasestatement.h"
#include "pdfshortcutrepository.h"

#include "pdfdocumenttoolcatalog.h"

#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"
#include "pdfwidgetstyle.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QGridLayout>
#include <QKeyEvent>
#include <QScreen>
#include <QVBoxLayout>

#include <algorithm>

namespace pdf
{

/// Helpers of the Tools panel
class PDFAppDocumentToolsPanelHelper
{
public:
    PDFAppDocumentToolsPanelHelper() = delete;

    /// Context of the texts shared with the other panels (addText)
    static constexpr const char* PANEL_CONTEXT = "pdf::PDFAppDocumentPanelPopup";

    /// Longest search text shown in the message of no results
    static constexpr qsizetype MAXIMUM_SHOWN_TEXT = 64;

    /// Text of an empty state (Regular 12 text/muted, wrapped)
    static PDFLabel* createEmptyLabel(const QString& objectName, QWidget* parent)
    {
        PDFLabel* label = new PDFLabel(QString(), PDFThemeFonts::Role::Small, PDFThemeColors::TextMuted, parent);
        label->setObjectName(objectName);
        label->setWordWrap(true);
        label->setTextFormat(Qt::PlainText);
        return label;
    }

    /// Invalidates the cached sizes of the children and the layouts of the panel:
    /// the panel computes its height from them (a hidden panel keeps the sizes of
    /// the layout items cached before its sections were filled)
    static void invalidateLayouts(QWidget* panel)
    {
        const QList<QWidget*> widgets = panel->findChildren<QWidget*>();
        for (QWidget* widget : widgets)
        {
            widget->updateGeometry();
        }

        const QList<QLayout*> layouts = panel->findChildren<QLayout*>();
        for (QLayout* layout : layouts)
        {
            layout->invalidate();
        }
        if (QLayout* layout = panel->layout())
        {
            layout->invalidate();
        }
    }

    /// Caption of a section: one line of 13 px (Medium 11 of 196:3656)
    static constexpr int CAPTION_HEIGHT = 13;

    /// Deletes the widgets of the list and clears it
    template<typename T>
    static void deleteWidgets(std::vector<QPointer<T>>& widgets)
    {
        for (const QPointer<T>& widget : widgets)
        {
            delete widget.data();
        }
        widgets.clear();
    }
};

PDFAppDocumentToolsPanel::PDFAppDocumentToolsPanel(PDFAppDocumentView* view, QWidget* parent) :
    BaseClass(view, PANEL_WIDTH, parent)
{
    using H = PDFAppDocumentToolsPanelHelper;

    setObjectName(QStringLiteral("toolsPanel"));
    setTitleText(QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Tools"));

    QVBoxLayout* layout = getContentLayout();

    // tools-search 196:3651: the search field of the whole width (366 x 36)
    m_searchField = new PDFLineEdit(this);
    m_searchField->setObjectName(QStringLiteral("toolsSearch"));
    m_searchField->setSearchField();
    m_searchField->setMaxLength(MAXIMUM_SEARCH_LENGTH);
    connect(m_searchField.data(), &QLineEdit::textChanged, this, &PDFAppDocumentToolsPanel::onSearchTextChanged);
    connect(m_searchField.data(), &QLineEdit::returnPressed, this, &PDFAppDocumentToolsPanel::onSearchReturnPressed);
    layout->addWidget(m_searchField);

    // The sections Pinned and Recent (14 px apart as the sections of the panel)
    m_sections = new QWidget(this);
    m_sections->setObjectName(QStringLiteral("toolsSections"));
    QVBoxLayout* sectionsLayout = PDFAppPageWidgets::createColumn(m_sections, SECTION_GAP);

    // section-pinned 196:3655: the caption and the grid of the Tool shortcuts (2 columns, gaps 8)
    m_pinnedSection = new QWidget(m_sections);
    m_pinnedSection->setObjectName(QStringLiteral("toolsPinned"));
    QVBoxLayout* pinnedLayout = PDFAppPageWidgets::createColumn(m_pinnedSection, SECTION_CONTENT_GAP);
    PDFLabel* pinnedCaption = createCaption(QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Pinned"));
    pinnedCaption->setParent(m_pinnedSection);
    pinnedCaption->setFixedHeight(H::CAPTION_HEIGHT);
    pinnedLayout->addWidget(pinnedCaption);
    m_pinnedGrid = new QWidget(m_pinnedSection);
    m_pinnedLayout = new QGridLayout(m_pinnedGrid);
    m_pinnedLayout->setContentsMargins(0, 0, 0, 0);
    m_pinnedLayout->setHorizontalSpacing(SHORTCUT_GAP);
    m_pinnedLayout->setVerticalSpacing(SHORTCUT_GAP);
    m_pinnedLayout->setColumnStretch(0, 1);
    m_pinnedLayout->setColumnStretch(1, 1);
    pinnedLayout->addWidget(m_pinnedGrid);
    m_pinnedEmpty = H::createEmptyLabel(QStringLiteral("toolsPinnedEmpty"), m_pinnedSection);
    pinnedLayout->addWidget(m_pinnedEmpty);
    sectionsLayout->addWidget(m_pinnedSection);

    // section-recent 196:3704: the caption and the rows 48 high without gaps
    m_recentSection = new QWidget(m_sections);
    m_recentSection->setObjectName(QStringLiteral("toolsRecent"));
    QVBoxLayout* recentLayout = PDFAppPageWidgets::createColumn(m_recentSection, SECTION_CONTENT_GAP);
    PDFLabel* recentCaption = createCaption(QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Recent"));
    recentCaption->setParent(m_recentSection);
    recentCaption->setFixedHeight(H::CAPTION_HEIGHT);
    recentLayout->addWidget(recentCaption);
    m_recentList = new QWidget(m_recentSection);
    m_recentLayout = PDFAppPageWidgets::createColumn(m_recentList, 0);
    recentLayout->addWidget(m_recentList);
    m_recentEmpty = H::createEmptyLabel(QStringLiteral("toolsRecentEmpty"), m_recentSection);
    recentLayout->addWidget(m_recentEmpty);
    sectionsLayout->addWidget(m_recentSection);
    layout->addWidget(m_sections);

    // Results of a search (not in Figma: the captions and rows of the panel and
    // the result rows of the global search 941:7081)
    m_resultsWidget = new QWidget(this);
    m_resultsWidget->setObjectName(QStringLiteral("toolsResults"));
    QVBoxLayout* resultsLayout = PDFAppPageWidgets::createColumn(m_resultsWidget, SECTION_CONTENT_GAP);
    m_toolsCaption = createCaption(QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Tools"));
    m_toolsCaption->setParent(m_resultsWidget);
    m_toolsCaption->setFixedHeight(H::CAPTION_HEIGHT);
    resultsLayout->addWidget(m_toolsCaption);
    m_toolResults = new QWidget(m_resultsWidget);
    m_toolResultsLayout = PDFAppPageWidgets::createColumn(m_toolResults, 0);
    resultsLayout->addWidget(m_toolResults);
    m_commandsCaption = createCaption(QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Actions"));
    m_commandsCaption->setParent(m_resultsWidget);
    m_commandsCaption->setFixedHeight(H::CAPTION_HEIGHT);
    resultsLayout->addWidget(m_commandsCaption);
    m_commandResults = new QWidget(m_resultsWidget);
    m_commandResultsLayout = PDFAppPageWidgets::createColumn(m_commandResults, 0);
    resultsLayout->addWidget(m_commandResults);
    m_noResults = H::createEmptyLabel(QStringLiteral("toolsNoResults"), m_resultsWidget);
    resultsLayout->addWidget(m_noResults);

    // The closest tools of a search without a result (O25, tools.md 9.1 point 4; not
    // in Figma: the caption of the sections and the result rows of the tools above)
    m_nearestCaption = createCaption(QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Closest tools"));
    m_nearestCaption->setParent(m_resultsWidget);
    m_nearestCaption->setObjectName(QStringLiteral("toolsNearestCaption"));
    m_nearestCaption->setFixedHeight(H::CAPTION_HEIGHT);
    resultsLayout->addWidget(m_nearestCaption);
    m_nearestResults = new QWidget(m_resultsWidget);
    m_nearestResults->setObjectName(QStringLiteral("toolsNearest"));
    m_nearestResultsLayout = PDFAppPageWidgets::createColumn(m_nearestResults, 0);
    resultsLayout->addWidget(m_nearestResults);
    m_resultsWidget->hide();
    layout->addWidget(m_resultsWidget);

    // divider 196:3737 and the Link row All tools 196:3738
    m_divider = createDivider();
    layout->addWidget(m_divider);
    m_allToolsRow = new PDFLinkRow(this);
    m_allToolsRow->setObjectName(QStringLiteral("allTools"));
    m_allToolsRow->setIconName(QStringLiteral("tools"));
    connect(m_allToolsRow.data(), &QAbstractButton::clicked, this, &PDFAppDocumentToolsPanel::onAllToolsClicked);
    layout->addWidget(m_allToolsRow);

    const PDFAppContext* context = view ? view->getContext() : nullptr;
    if (PDFDatabaseNotifier* notifier = context ? context->getNotifier() : nullptr)
    {
        connect(notifier, &PDFDatabaseNotifier::changed, this, &PDFAppDocumentToolsPanel::onDatabaseChanged);
        connect(notifier, &PDFDatabaseNotifier::everythingChanged, this, &PDFAppDocumentToolsPanel::onDatabaseEverythingChanged);
    }

    // O25: the times of the recent tools follow the clock while the panel is open
    m_minuteSubscription = new PDFAppMinuteSubscription(context ? context->getMinuteTicker() : nullptr, this);
    connect(m_minuteSubscription, &PDFAppMinuteSubscription::minuteChanged, this, &PDFAppDocumentToolsPanel::onMinuteChanged);

    updateTexts();
    updateMode();
}

PDFAppDocumentToolsPanel::~PDFAppDocumentToolsPanel() = default;

PDFLineEdit* PDFAppDocumentToolsPanel::getSearchField() const
{
    return m_searchField.data();
}

PDFLinkRow* PDFAppDocumentToolsPanel::getAllToolsRow() const
{
    return m_allToolsRow.data();
}

std::vector<PDFToolShortcut*> PDFAppDocumentToolsPanel::getPinnedShortcuts() const
{
    std::vector<PDFToolShortcut*> shortcuts;
    for (const QPointer<PDFToolShortcut>& shortcut : m_shortcuts)
    {
        if (shortcut)
        {
            shortcuts.push_back(shortcut.data());
        }
    }
    return shortcuts;
}

std::vector<PDFRecentToolRow*> PDFAppDocumentToolsPanel::getRecentRows() const
{
    std::vector<PDFRecentToolRow*> rows;
    for (const QPointer<PDFRecentToolRow>& row : m_recentRows)
    {
        if (row)
        {
            rows.push_back(row.data());
        }
    }
    return rows;
}

std::vector<PDFAppSearchResultRow*> PDFAppDocumentToolsPanel::getResultRows() const
{
    std::vector<PDFAppSearchResultRow*> rows;
    for (const QPointer<PDFAppSearchResultRow>& row : m_resultRows)
    {
        if (row)
        {
            rows.push_back(row.data());
        }
    }
    return rows;
}

std::vector<PDFAppSearchResultRow*> PDFAppDocumentToolsPanel::getNearestRows() const
{
    std::vector<PDFAppSearchResultRow*> rows;
    for (const QPointer<PDFAppSearchResultRow>& row : m_nearestRows)
    {
        if (row)
        {
            rows.push_back(row.data());
        }
    }
    return rows;
}

PDFLabel* PDFAppDocumentToolsPanel::getPinnedEmptyLabel() const
{
    return m_shortcuts.empty() ? m_pinnedEmpty.data() : nullptr;
}

PDFLabel* PDFAppDocumentToolsPanel::getRecentEmptyLabel() const
{
    return m_recentRows.empty() ? m_recentEmpty.data() : nullptr;
}

PDFLabel* PDFAppDocumentToolsPanel::getNoResultsLabel() const
{
    return isSearching() && m_resultRows.empty() ? m_noResults.data() : nullptr;
}

bool PDFAppDocumentToolsPanel::isSearching() const
{
    return m_searchField && !m_searchField->text().trimmed().isEmpty();
}

PDFAppDocument* PDFAppDocumentToolsPanel::getDocument() const
{
    const PDFAppDocumentView* view = getView();
    return view ? view->getDocument() : nullptr;
}

bool PDFAppDocumentToolsPanel::isDocumentLocked() const
{
    const PDFAppDocument* document = getDocument();
    return document && (document->isLocked() || document->isPagedXFA());
}

bool PDFAppDocumentToolsPanel::isToolAvailable(const QString& toolId) const
{
    const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(toolId);
    if (!tool)
    {
        return false;
    }

    // tools.md 4.7: a locked document disables the tools, which change it
    return tool->getLockedAccess() != PDFDocumentToolLockedAccess::Unavailable || !isDocumentLocked();
}

PDFAppRoute PDFAppDocumentToolsPanel::getToolRoute(const QString& toolId) const
{
    const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(toolId);
    if (!tool)
    {
        return PDFAppRoute();
    }

    // A tool without a document opens its page in the Home tab, the others work
    // on the document of the view
    const PDFAppDocument* document = getDocument();
    if (tool->getLockedAccess() == PDFDocumentToolLockedAccess::NoDocument || !document || document->getId().isEmpty())
    {
        return PDFAppRoutes::tool(tool->getId());
    }
    return PDFAppRoutes::documentTool(document->getId(), tool->getId());
}

QString PDFAppDocumentToolsPanel::getLockedToolTip()
{
    //: Tooltip of a tool of the Tools panel, which would change a locked document
    return QCoreApplication::translate("pdf::PDFAppDocumentToolsPanel", "Not available for a locked document. Unlock it or use Edit a Copy.");
}

bool PDFAppDocumentToolsPanel::activateTool(const QString& toolId)
{
    if (!isToolAvailable(toolId))
    {
        return false;
    }

    const PDFAppRoute route = getToolRoute(toolId);
    if (!route.isValid())
    {
        return false;
    }

    navigateQueued(route);
    return true;
}

void PDFAppDocumentToolsPanel::navigateQueued(const PDFAppRoute& route)
{
    // The panel closes first, the navigation runs from the event loop (it may
    // replace the page of the tab and delete the panel); a deleted window drops it
    const PDFAppDocumentView* view = getView();
    QPointer<PDFAppMainWindow> window = view ? qobject_cast<PDFAppMainWindow*>(view->window()) : nullptr;
    close();
    if (window && window->getNavigator())
    {
        QMetaObject::invokeMethod(window->getNavigator(), &PDFAppNavigator::navigate, Qt::QueuedConnection, route, PDFAppNavigationMode::Push, static_cast<PDFAppTab*>(nullptr));
    }
}

void PDFAppDocumentToolsPanel::updateState()
{
    BaseClass::updateState();

    if (!isVisible())
    {
        // The opening: a new search, the data of the database now (another
        // instance may have changed them while the panel was closed); the keyboard
        // starts at the search field (PDFAppDocumentPanelPopup::popup focuses the
        // first control of the panel)
        if (m_searchField && !m_searchField->text().isEmpty())
        {
            const QSignalBlocker blocker(m_searchField.data());
            m_searchField->clear();
        }

        const PDFAppContext* context = getView() ? getView()->getContext() : nullptr;
        m_searchData = PDFAppSearchData();
        m_searchData.includeVideos = false;
        m_searchData.commandRegistry = context ? context->getCommandRegistry() : nullptr;
        if (context && context->getDatabase())
        {
            PDFDatabaseError error;
            m_searchData.userShortcuts = context->getShortcuts().getShortcuts(&error);
        }

        readData();
        rebuildPinned(false);
        rebuildRecent(false);
        rebuildResults();
        updateMode();

        // The opening computes the size of the panel from the layouts
        PDFAppDocumentToolsPanelHelper::invalidateLayouts(this);
    }

    updateAvailability();
}

void PDFAppDocumentToolsPanel::readData()
{
    // One consistent snapshot of the pins and of the use (empty without a database)
    ++m_readCount;
    const PDFAppContext* context = getView() ? getView()->getContext() : nullptr;
    PDFAppToolState state = PDFAppToolState::readPinsAndUsage(context ? context->getDatabase() : nullptr, MAXIMUM_RECENT);
    m_pinnedTools = state.pinnedTools.mid(0, MAXIMUM_PINNED);
    m_usage = std::move(state.usage);
}

void PDFAppDocumentToolsPanel::rebuildPinned(bool force)
{
    if (!m_pinnedLayout || (!force && m_shownPinned == m_pinnedTools && m_shortcuts.size() == size_t(m_shownPinned.size())))
    {
        return;
    }

    // The keyboard stays at the same tool
    const QString focusedTool = PDFAppToolPresentation::getToolId(focusWidget());
    bool hadFocus = false;
    for (const QPointer<PDFToolShortcut>& shortcut : m_shortcuts)
    {
        hadFocus = hadFocus || (shortcut && shortcut.data() == focusWidget());
    }

    PDFAppDocumentToolsPanelHelper::deleteWidgets(m_shortcuts);
    m_shownPinned = m_pinnedTools;

    QWidget* newFocus = nullptr;
    for (const QString& toolId : std::as_const(m_shownPinned))
    {
        const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(toolId);
        if (!tool)
        {
            continue;
        }

        const int index = int(m_shortcuts.size());
        PDFToolShortcut* shortcut = PDFAppToolPresentation::createToolShortcut(*tool, m_pinnedGrid);
        connect(shortcut, &QAbstractButton::clicked, this, &PDFAppDocumentToolsPanel::onShortcutClicked);
        m_pinnedLayout->addWidget(shortcut, index / 2, index % 2);
        shortcut->show();
        m_shortcuts.emplace_back(shortcut);
        if (hadFocus && toolId == focusedTool)
        {
            newFocus = shortcut;
        }
    }

    m_pinnedGrid->setVisible(!m_shortcuts.empty());
    m_pinnedEmpty->setVisible(m_shortcuts.empty());
    if (hadFocus)
    {
        QWidget* focus = newFocus ? newFocus : static_cast<QWidget*>(m_searchField.data());
        if (focus)
        {
            focus->setFocus(Qt::OtherFocusReason);
        }
    }
}

void PDFAppDocumentToolsPanel::rebuildRecent(bool force)
{
    if (!m_recentLayout)
    {
        return;
    }

    // The times, not their texts: the minute ticker updates the texts (O25)
    const qint64 now = PDFAppHomeFormat::getCurrentTime();
    QStringList signature;
    for (const PDFToolUsage& usage : m_usage)
    {
        signature << usage.toolId << QString::number(usage.lastUsed);
    }
    if (!force && signature == m_shownRecent)
    {
        updateRecentTimes(now);
        return;
    }

    const QString focusedTool = PDFAppToolPresentation::getToolId(focusWidget());
    bool hadFocus = false;
    for (const QPointer<PDFRecentToolRow>& row : m_recentRows)
    {
        hadFocus = hadFocus || (row && row.data() == focusWidget());
    }

    PDFAppDocumentToolsPanelHelper::deleteWidgets(m_recentRows);
    m_shownRecent = signature;

    QWidget* newFocus = nullptr;
    for (const PDFToolUsage& usage : m_usage)
    {
        const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(usage.toolId);
        if (!tool)
        {
            continue;
        }

        PDFRecentToolRow* row = PDFAppToolPresentation::createRecentToolRow(*tool, usage.lastUsed, now, m_recentList);
        connect(row, &QAbstractButton::clicked, this, &PDFAppDocumentToolsPanel::onRecentClicked);
        m_recentLayout->addWidget(row);
        row->show();
        m_recentRows.emplace_back(row);
        if (hadFocus && usage.toolId == focusedTool)
        {
            newFocus = row;
        }
    }

    m_recentList->setVisible(!m_recentRows.empty());
    m_recentEmpty->setVisible(m_recentRows.empty());
    updateRecentTimes(now);
    if (hadFocus)
    {
        QWidget* focus = newFocus ? newFocus : static_cast<QWidget*>(m_searchField.data());
        if (focus)
        {
            focus->setFocus(Qt::OtherFocusReason);
        }
    }
}

void PDFAppDocumentToolsPanel::rebuildResults()
{
    bool hadFocus = false;
    for (const QPointer<PDFAppSearchResultRow>& row : m_resultRows)
    {
        hadFocus = hadFocus || (row && row.data() == focusWidget());
    }
    for (const QPointer<PDFAppSearchResultRow>& row : m_nearestRows)
    {
        hadFocus = hadFocus || (row && row.data() == focusWidget());
    }

    PDFAppDocumentToolsPanelHelper::deleteWidgets(m_resultRows);
    PDFAppDocumentToolsPanelHelper::deleteWidgets(m_nearestRows);
    m_results = isSearching() ? m_engine.search(m_searchField->text(), m_searchData) : PDFAppSearchResults();
    m_results.isEmptyQuery = m_results.isEmptyQuery || !isSearching();

    // Tools first, then the commands (the order of the global search)
    int toolCount = 0;
    int commandCount = 0;
    for (const PDFAppSearchGroup group : { PDFAppSearchGroup::Tools, PDFAppSearchGroup::Commands })
    {
        const PDFAppSearchGroupResults* groupResults = m_results.isEmptyQuery ? nullptr : m_results.getGroup(group);
        if (!groupResults)
        {
            continue;
        }

        const bool isTools = group == PDFAppSearchGroup::Tools;
        QWidget* list = isTools ? m_toolResults.data() : m_commandResults.data();
        QVBoxLayout* listLayout = isTools ? m_toolResultsLayout.data() : m_commandResultsLayout.data();
        int& count = isTools ? toolCount : commandCount;
        for (const PDFAppSearchResult& result : groupResults->results)
        {
            if (count >= MAXIMUM_GROUP_RESULTS || !list || !listLayout)
            {
                break;
            }

            PDFAppSearchResultRow* row = new PDFAppSearchResultRow(result, list);
            row->setObjectName(QStringLiteral("result:") + result.key);
            connect(row, &QAbstractButton::clicked, this, &PDFAppDocumentToolsPanel::onResultClicked);
            listLayout->addWidget(row);
            row->show();
            m_resultRows.emplace_back(row);
            ++count;
        }
    }

    const bool searching = isSearching();
    m_toolsCaption->setVisible(searching && toolCount > 0);
    m_toolResults->setVisible(searching && toolCount > 0);
    m_commandsCaption->setVisible(searching && commandCount > 0);
    m_commandResults->setVisible(searching && commandCount > 0);
    m_noResults->setVisible(searching && m_resultRows.empty());
    rebuildNearest();
    updateTexts();

    if (hadFocus && m_searchField)
    {
        m_searchField->setFocus(Qt::OtherFocusReason);
    }
}

void PDFAppDocumentToolsPanel::rebuildNearest()
{
    // A search without a tool or an action offers up to three nearest tools, which
    // can run on this document (O25); none, when no tool is close enough
    if (isSearching() && m_resultRows.empty() && m_nearestResults && m_nearestResultsLayout)
    {
        QStringList unavailableTools;
        for (const PDFDocumentToolCatalogEntry& tool : PDFDocumentToolCatalog::getTools())
        {
            if (!isToolAvailable(tool.getId()))
            {
                unavailableTools << tool.getId();
            }
        }

        int order = 0;
        for (const PDFDocumentToolSearchMatch& match : PDFDocumentToolSearch::searchNearest(m_searchField->text(), unavailableTools))
        {
            const PDFAppSearchResult result = PDFAppToolSearchProvider::createResult(match, order++);
            if (result.key.isEmpty())
            {
                continue;
            }

            PDFAppSearchResultRow* row = new PDFAppSearchResultRow(result, m_nearestResults);
            row->setObjectName(QStringLiteral("nearest:") + result.key);
            connect(row, &QAbstractButton::clicked, this, &PDFAppDocumentToolsPanel::onResultClicked);
            m_nearestResultsLayout->addWidget(row);
            row->show();
            m_nearestRows.emplace_back(row);
        }
    }

    const bool isShown = !m_nearestRows.empty();
    if (m_nearestCaption)
    {
        m_nearestCaption->setVisible(isShown);
    }
    if (m_nearestResults)
    {
        m_nearestResults->setVisible(isShown);
    }
}

void PDFAppDocumentToolsPanel::updateTexts()
{
    if (m_searchField)
    {
        m_searchField->setPlaceholderText(tr("Search tools and actions…"));
        m_searchField->setAccessibleName(tr("Search tools and actions"));
    }
    if (m_pinnedEmpty)
    {
        m_pinnedEmpty->setText(tr("No pinned tools yet. Pin a tool with its ★ in the catalog to keep it here."));
    }
    if (m_recentEmpty)
    {
        m_recentEmpty->setText(tr("The tools you use appear here."));
    }
    if (m_noResults && isSearching())
    {
        const QString text = PDFAppSearchEngine::getVisibleText(m_searchField->text().trimmed(), PDFAppDocumentToolsPanelHelper::MAXIMUM_SHOWN_TEXT);
        m_noResults->setText(tr("No tools or actions match “%1”.").arg(text));
    }
    if (m_allToolsRow)
    {
        m_allToolsRow->setTitle(tr("All tools"));
        m_allToolsRow->setSubtitle(tr("Browse the complete catalog of %Ln tools", nullptr, PDFDocumentToolCatalog::getToolCount()));
    }
}

void PDFAppDocumentToolsPanel::updateAvailability()
{
    // A display version of an XFA form (step 8) cannot be unlocked: its own reason
    const PDFAppDocument* document = getDocument();
    const QString lockedToolTip = document && document->isPagedXFA() ? PDFAppDocument::getPagedXFARefusalText() : getLockedToolTip();

    // Tool shortcuts and rows: disabled by the lock (text/disabled, icon/disabled
    // of the components), the tooltip says why
    std::vector<QAbstractButton*> toolButtons;
    for (const QPointer<PDFToolShortcut>& shortcut : m_shortcuts)
    {
        toolButtons.push_back(shortcut.data());
    }
    for (const QPointer<PDFRecentToolRow>& row : m_recentRows)
    {
        toolButtons.push_back(row.data());
    }
    for (QAbstractButton* button : toolButtons)
    {
        if (!button)
        {
            continue;
        }

        const QString toolId = PDFAppToolPresentation::getToolId(button);
        const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(toolId);
        const bool isAvailable = isToolAvailable(toolId);
        button->setEnabled(isAvailable);
        const QString description = tool ? tool->getDescription() : QString();
        button->setToolTip(isAvailable ? description : lockedToolTip);
        if (qobject_cast<PDFToolShortcut*>(button))
        {
            button->setAccessibleDescription(isAvailable ? description : lockedToolTip);
        }
    }

    // Results: a tool as its shortcut, a command by its state on this view
    const PDFAppDocumentView* view = getView();
    PDFAppSearchResultRow* current = nullptr;
    for (const QPointer<PDFAppSearchResultRow>& row : m_resultRows)
    {
        if (!row)
        {
            continue;
        }

        const PDFAppSearchResult& result = row->getResult();
        bool isAvailable = false;
        if (result.group == PDFAppSearchGroup::Tools)
        {
            isAvailable = isToolAvailable(result.key);
            row->setToolTip(isAvailable ? QString() : lockedToolTip);
        }
        else
        {
            isAvailable = view && view->isCommandEnabled(result.commandId);
        }
        row->setEnabled(isAvailable);
        if (isAvailable && !current)
        {
            current = row.data();
        }
    }

    // The closest tools (a lock after the search): never the current result - Enter of
    // the field does not open a guess
    for (const QPointer<PDFAppSearchResultRow>& row : m_nearestRows)
    {
        if (row)
        {
            const bool isAvailable = isToolAvailable(row->getResult().key);
            row->setToolTip(isAvailable ? QString() : lockedToolTip);
            row->setEnabled(isAvailable);
        }
    }

    // The current result (Enter of the field opens it): the focused row, else the first enabled one
    PDFAppSearchResultRow* focused = qobject_cast<PDFAppSearchResultRow*>(focusWidget());
    setCurrentResultRow(focused && focused->isEnabled() ? focused : current);
}

void PDFAppDocumentToolsPanel::setCurrentResultRow(PDFAppSearchResultRow* row)
{
    for (const QPointer<PDFAppSearchResultRow>& item : m_resultRows)
    {
        if (item)
        {
            item->setCurrent(item.data() == row);
        }
    }
}

void PDFAppDocumentToolsPanel::updateMode()
{
    const bool searching = isSearching();
    if (m_sections)
    {
        m_sections->setVisible(!searching);
    }
    if (m_resultsWidget)
    {
        m_resultsWidget->setVisible(searching);
    }
    updateTabOrder();
}

std::vector<QWidget*> PDFAppDocumentToolsPanel::getFocusChain() const
{
    std::vector<QWidget*> chain;
    chain.push_back(m_searchField.data());
    for (const QPointer<PDFToolShortcut>& shortcut : m_shortcuts)
    {
        chain.push_back(shortcut.data());
    }
    for (const QPointer<PDFRecentToolRow>& row : m_recentRows)
    {
        chain.push_back(row.data());
    }
    for (const QPointer<PDFAppSearchResultRow>& row : m_resultRows)
    {
        chain.push_back(row.data());
    }
    for (const QPointer<PDFAppSearchResultRow>& row : m_nearestRows)
    {
        chain.push_back(row.data());
    }
    chain.push_back(m_allToolsRow.data());
    return chain;
}

void PDFAppDocumentToolsPanel::updateTabOrder()
{
    // The order of the panel (widgets created later would be at the end of the chain)
    const std::vector<QWidget*> chain = getFocusChain();
    QWidget* previous = nullptr;
    for (QWidget* widget : chain)
    {
        if (!widget)
        {
            continue;
        }
        if (previous)
        {
            QWidget::setTabOrder(previous, widget);
        }
        previous = widget;
    }
}

void PDFAppDocumentToolsPanel::watchChildren()
{
    // Left / Right of the controls created after the opening reach the panel (the
    // filter of PDFAppDocumentPanelPopup), the focus of a result makes it current
    const QList<QWidget*> controls = findChildren<QWidget*>();
    for (QWidget* control : controls)
    {
        control->installEventFilter(this);
    }
}

void PDFAppDocumentToolsPanel::updatePanelHeight()
{
    QVBoxLayout* layout = getContentLayout();
    if (!isVisible() || !layout)
    {
        return;
    }

    // The content changed while the panel is open: the panel keeps its width and
    // its top edge, its height follows the content; a panel, which does not fit
    // into the screen any more, moves up (as at the opening)
    PDFAppDocumentToolsPanelHelper::invalidateLayouts(this);
    layout->activate();
    const QMargins margins = PDFWidgetStyle::getPopoverMargins(this);
    const int windowWidth = width();
    const int windowHeight = layout->hasHeightForWidth() ? layout->totalHeightForWidth(windowWidth) : layout->totalSizeHint().height();
    QRect panel(QPoint(x() + margins.left(), y() + margins.top()), QSize(windowWidth - margins.left() - margins.right(), qMax(0, windowHeight - margins.top() - margins.bottom())));
    if (const QScreen* screen = this->screen())
    {
        const QRect available = screen->availableGeometry();
        if (panel.bottom() > available.bottom())
        {
            panel.moveTop(qMax(available.top(), available.bottom() + 1 - panel.height()));
        }
        if (panel.height() > available.height())
        {
            panel.setHeight(available.height());
        }
    }
    setFixedSize(panel.size().grownBy(margins));
    move(panel.topLeft() - QPoint(margins.left(), margins.top()));
}

void PDFAppDocumentToolsPanel::retranslate()
{
    BaseClass::retranslate();

    // The titles of the catalog and the times are translated: the widgets again
    updateTexts();
    rebuildPinned(true);
    rebuildRecent(true);
    rebuildResults();
    updateMode();
    updateAvailability();
    watchChildren();
    updatePanelHeight();
}

void PDFAppDocumentToolsPanel::scheduleRefresh()
{
    // A closed panel reads the data, when it opens
    if (!isVisible() || m_isRefreshPending)
    {
        return;
    }

    m_isRefreshPending = true;
    QMetaObject::invokeMethod(this, &PDFAppDocumentToolsPanel::onRefreshQueued, Qt::QueuedConnection);
}

void PDFAppDocumentToolsPanel::updateRecentTimes(qint64 now)
{
    for (const QPointer<PDFRecentToolRow>& row : m_recentRows)
    {
        PDFAppHomeFormat::updateTimeTexts(row, now);
    }
    if (m_minuteSubscription)
    {
        m_minuteSubscription->setUpdated(now);
    }
}

void PDFAppDocumentToolsPanel::onMinuteChanged(qint64 now)
{
    // Only the texts: no read of the database, no rebuild of the rows
    updateRecentTimes(now);
}

void PDFAppDocumentToolsPanel::onRefreshQueued()
{
    m_isRefreshPending = false;
    if (!isVisible())
    {
        return;
    }

    readData();
    rebuildPinned(false);
    rebuildRecent(false);
    updateTabOrder();
    updateAvailability();
    watchChildren();
    updatePanelHeight();
}

void PDFAppDocumentToolsPanel::onDatabaseChanged(const QString& topic, const QString& key)
{
    Q_UNUSED(key);

    if (topic == PDFToolRepository::getPinTopic() || topic == PDFToolRepository::getUsageTopic())
    {
        scheduleRefresh();
    }
}

void PDFAppDocumentToolsPanel::onDatabaseEverythingChanged()
{
    scheduleRefresh();
}

void PDFAppDocumentToolsPanel::onSearchTextChanged()
{
    rebuildResults();
    updateMode();
    updateAvailability();
    watchChildren();
    updatePanelHeight();
}

void PDFAppDocumentToolsPanel::onSearchReturnPressed()
{
    // Enter opens the current result (the first enabled one)
    for (const QPointer<PDFAppSearchResultRow>& row : m_resultRows)
    {
        if (row && row->isCurrent() && row->isEnabled())
        {
            activateResult(row.data());
            return;
        }
    }
}

void PDFAppDocumentToolsPanel::onShortcutClicked()
{
    // The shortcuts do not identify themselves in the signal
    activateTool(PDFAppToolPresentation::getToolId(sender()));
}

void PDFAppDocumentToolsPanel::onRecentClicked()
{
    activateTool(PDFAppToolPresentation::getToolId(sender()));
}

void PDFAppDocumentToolsPanel::onResultClicked()
{
    activateResult(qobject_cast<PDFAppSearchResultRow*>(sender()));
}

void PDFAppDocumentToolsPanel::activateResult(PDFAppSearchResultRow* row)
{
    if (!row || !row->isEnabled())
    {
        return;
    }

    // A copy: the panel closes and the rows may go
    const PDFAppSearchResult result = row->getResult();
    if (result.group == PDFAppSearchGroup::Tools)
    {
        activateTool(result.key);
    }
    else if (result.action == PDFAppSearchResult::Action::RunCommand && !result.commandId.isEmpty())
    {
        // The command runs on the view of this panel
        runCommand(result.commandId);
    }
}

void PDFAppDocumentToolsPanel::onAllToolsClicked()
{
    // The catalog in the Home tab, with the text of the search
    const QString text = m_searchField ? m_searchField->text().trimmed() : QString();
    navigateQueued(PDFAppRoutes::tools(QString(), text));
}

void PDFAppDocumentToolsPanel::moveFocus(bool isNext)
{
    // Up / Down in the order of the panel over the shown and enabled controls (the
    // focus chain of Qt starts at the focus widget of the application, which need
    // not be in the panel)
    std::vector<QWidget*> chain;
    for (QWidget* widget : getFocusChain())
    {
        if (widget && widget->isVisibleTo(this) && widget->isEnabled())
        {
            chain.push_back(widget);
        }
    }
    if (chain.empty())
    {
        return;
    }

    const auto it = std::find(chain.cbegin(), chain.cend(), focusWidget());
    QWidget* target = nullptr;
    if (it == chain.cend())
    {
        target = isNext ? chain.front() : chain.back();
    }
    else if (isNext)
    {
        target = std::next(it) != chain.cend() ? *std::next(it) : nullptr;
    }
    else
    {
        target = it != chain.cbegin() ? *std::prev(it) : nullptr;
    }

    if (target)
    {
        target->setFocus(isNext ? Qt::TabFocusReason : Qt::BacktabFocusReason);
    }
}

void PDFAppDocumentToolsPanel::keyPressEvent(QKeyEvent* event)
{
    if ((event->key() == Qt::Key_Down || event->key() == Qt::Key_Up) && (event->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier)
    {
        moveFocus(event->key() == Qt::Key_Down);
        event->accept();
        return;
    }

    // Esc clears the search first (the sections come back), then it closes the panel
    if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier && m_searchField && !m_searchField->text().isEmpty())
    {
        m_searchField->clear();
        m_searchField->setFocus(Qt::OtherFocusReason);
        event->accept();
        return;
    }

    BaseClass::keyPressEvent(event);
}

bool PDFAppDocumentToolsPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::FocusIn)
    {
        if (PDFAppSearchResultRow* row = qobject_cast<PDFAppSearchResultRow*>(watched))
        {
            if (row->isEnabled() && isAncestorOf(row))
            {
                setCurrentResultRow(row);
            }
        }
    }

    return BaseClass::eventFilter(watched, event);
}

}   // namespace pdf
