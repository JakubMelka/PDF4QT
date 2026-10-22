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

#include "pdfappdocumentsidebar.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_attachments.h"
#include "pdfappdocumentpanels_bookmarks.h"
#include "pdfappdocumentpanels_layers.h"
#include "pdfappdocumentpanels_notes.h"
#include "pdfappdocumentpanels_outline.h"
#include "pdfappdocumentpanels_thumbnails.h"
#include "pdfappdocumentview.h"
#include "pdfappworkspace.h"
#include "pdfdatabasenotifier.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"

#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyleproperties.h"

#include <QButtonGroup>
#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace pdf
{

void PDFAppDocumentPanelRegistry::registerNavigationPanels(PDFAppDocumentPanelRegistry* registry)
{
    if (!registry)
    {
        return;
    }

    registry->registerFactory(std::make_shared<PDFAppOutlinePanelFactory>());
    registry->registerFactory(std::make_shared<PDFAppThumbnailsPanelFactory>());
    registry->registerFactory(std::make_shared<PDFAppLayersPanelFactory>());
    registry->registerFactory(std::make_shared<PDFAppAttachmentsPanelFactory>());
    registry->registerFactory(std::make_shared<PDFAppBookmarksPanelFactory>());
    registry->registerFactory(std::make_shared<PDFAppNotesPanelFactory>());
}

/// Sizes of the tabs of the setting viewing.sidebarIconSize
class PDFAppDocumentSidebarHelper
{
public:
    PDFAppDocumentSidebarHelper() = delete;

    /// Size of the tab and of its icon (Medium = the design: 32 / 18)
    static void getTabSize(const QString& value, int* tabSize, int* iconSize)
    {
        if (value == QLatin1String("small"))
        {
            *tabSize = 28;
            *iconSize = 16;
        }
        else if (value == QLatin1String("large"))
        {
            *tabSize = 36;
            *iconSize = 20;
        }
        else if (value == QLatin1String("veryLarge"))
        {
            *tabSize = 40;
            *iconSize = 24;
        }
        else
        {
            *tabSize = 32;
            *iconSize = 18;
        }
    }
};

PDFAppDocumentSidebar::PDFAppDocumentSidebar(PDFAppDocumentView* view, QWidget* parent) :
    BaseClass(parent),
    m_view(view),
    m_registry(PDFAppDocumentPanelRegistry::createDefault())
{
    setObjectName(QStringLiteral("documentSidebar"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 1, 0);
    layout->setSpacing(0);

    // sidebar-tabs 193:884
    m_tabsBar = new QWidget(this);
    m_tabsBar->setObjectName(QStringLiteral("sidebarTabs"));
    QHBoxLayout* tabsLayout = new QHBoxLayout(m_tabsBar);
    tabsLayout->setContentsMargins(TABS_PADDING_SIDE, TABS_PADDING_TOP, TABS_PADDING_SIDE, TABS_PADDING_BOTTOM);
    tabsLayout->setSpacing(TABS_GAP);
    layout->addWidget(m_tabsBar);

    m_tabGroup = new QButtonGroup(this);
    m_tabGroup->setExclusive(true);
    int buttonId = 0;
    for (const PDFAppDocumentPanelFactory* factory : m_registry.getFactories())
    {
        if (!factory->hasSidebarTab())
        {
            continue;
        }

        TabInfo info;
        info.panelId = factory->getId();
        info.factory = factory;
        QToolButton* button = new QToolButton(m_tabsBar);
        button->setObjectName(QStringLiteral("sidebarTab_%1").arg(info.panelId));
        PDFWidgetStyleProperties::set(button, PDFWidgetStyleProperties::SIDEBAR_TAB);
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::TabFocus);
        info.button = button;
        tabsLayout->addWidget(button);
        m_tabGroup->addButton(button, buttonId++);
        m_tabs.push_back(info);
    }
    tabsLayout->addStretch(1);
    connect(m_tabGroup, &QButtonGroup::idClicked, this, &PDFAppDocumentSidebar::onTabClicked);

    // sidebar-header 193:917
    m_header = new QWidget(this);
    m_header->setObjectName(QStringLiteral("sidebarHeader"));
    m_header->setMinimumHeight(HEADER_MINIMUM_HEIGHT);
    m_headerLayout = new QHBoxLayout(m_header);
    m_headerLayout->setContentsMargins(HEADER_PADDING_LEFT, HEADER_PADDING_TOP, HEADER_PADDING_RIGHT, HEADER_PADDING_BOTTOM);
    m_headerLayout->setSpacing(HEADER_GAP);

    QWidget* titleColumn = new QWidget(m_header);
    titleColumn->setObjectName(QStringLiteral("sidebarHeaderTitles"));
    QVBoxLayout* titleLayout = new QVBoxLayout(titleColumn);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(1);
    m_headerTitle = new PDFAppHeaderText(PDFThemeFonts::getFont(QFont::DemiBold, 14), PDFThemeColors::TextPrimary, titleColumn);
    m_headerTitle->setObjectName(QStringLiteral("sidebarTitle"));
    m_headerSubtitle = new PDFAppHeaderText(PDFThemeFonts::getFont(QFont::Normal, 11), PDFThemeColors::TextMuted, titleColumn);
    m_headerSubtitle->setObjectName(QStringLiteral("sidebarSubtitle"));
    titleLayout->addWidget(m_headerTitle);
    titleLayout->addWidget(m_headerSubtitle);
    m_headerLayout->addWidget(titleColumn, 1, Qt::AlignVCenter);
    layout->addWidget(m_header);

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("sidebarPanels"));
    layout->addWidget(m_stack, 1);

    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppDocumentSidebar::update));

    PDFAppContext* context = view ? view->getContext() : nullptr;
    if (context && context->getNotifier())
    {
        connect(context->getNotifier(), &PDFDatabaseNotifier::changed, this, &PDFAppDocumentSidebar::onSettingChanged);
        connect(context->getNotifier(), &PDFDatabaseNotifier::everythingChanged, this, &PDFAppDocumentSidebar::onEverythingChanged);
    }

    updateTabTexts();
    updateTabSizes();
    updateTabs();
    updateHeader();
}

PDFAppDocumentSidebar::~PDFAppDocumentSidebar()
{
    // The panels are children, which ~QWidget deletes after the members of the
    // sidebar are gone: they are disconnected (and deleted) while the sidebar is
    // whole, so no slot of the sidebar runs on a half-destroyed object
    if (m_document)
    {
        m_document->disconnect(this);
    }

    std::vector<QPointer<PDFAppDocumentPanel>> panels;
    for (const auto& item : m_panels)
    {
        panels.push_back(item.second);
    }
    m_panels.clear();
    for (const QPointer<PDFAppDocumentPanel>& panel : panels)
    {
        if (panel)
        {
            panel->disconnect(this);
            delete panel.data();
        }
    }

    for (const auto& item : m_headerWidgets)
    {
        delete item.second.data();
    }
    m_headerWidgets.clear();
}

void PDFAppDocumentSidebar::setDocument(PDFAppDocument* document)
{
    if (m_document == document)
    {
        return;
    }

    if (m_document)
    {
        m_document->disconnect(this);
    }

    m_document = document;
    if (document)
    {
        connect(document, &PDFAppDocument::stateChanged, this, &PDFAppDocumentSidebar::onDocumentStateChanged);
        connect(document, &PDFAppDocument::documentChanged, this, &PDFAppDocumentSidebar::onDocumentContentChanged);
        connect(document, &PDFAppDocument::signatureResultsChanged, this, &PDFAppDocumentSidebar::onDocumentContentChanged);
        connect(document, &QObject::destroyed, this, &PDFAppDocumentSidebar::onDocumentDestroyed);
    }

    // Copy of the panels: a panel may change the sidebar from setDocument
    std::vector<QPointer<PDFAppDocumentPanel>> panels;
    for (const auto& item : m_panels)
    {
        panels.push_back(item.second);
    }

    QPointer<PDFAppDocumentSidebar> self(this);
    for (const QPointer<PDFAppDocumentPanel>& panel : panels)
    {
        if (panel)
        {
            panel->setDocument(document, m_view);
        }
        if (!self)
        {
            return;
        }
    }

    onDocumentStateChanged();
}

bool PDFAppDocumentSidebar::showPanel(const QString& panelId)
{
    const PDFAppDocumentPanelFactory* factory = m_registry.findFactory(panelId);
    if (!factory || !m_stack)
    {
        return false;
    }

    const bool isReady = m_document && m_document->isReady();
    if (!isReady)
    {
        // The content of the document is not known yet: the panel is shown, when
        // the document is ready (and only, if it has content)
        m_requestedPanelId = panelId;
    }
    else if (!isPanelAvailable(panelId))
    {
        return false;
    }

    PDFAppDocumentPanel* panel = createPanel(panelId);
    if (!panel || !m_stack)
    {
        return false;
    }

    m_stack->setCurrentWidget(panel);
    for (const TabInfo& tab : m_tabs)
    {
        if (tab.button && tab.panelId == panelId && !tab.button->isChecked())
        {
            tab.button->setChecked(true);
        }
    }

    // A panel without a tab (search) leaves the tabs unchecked
    if (factory->hasSidebarTab() == false && m_tabGroup)
    {
        QAbstractButton* checked = m_tabGroup->checkedButton();
        if (checked)
        {
            m_tabGroup->setExclusive(false);
            checked->setChecked(false);
            m_tabGroup->setExclusive(true);
        }
    }

    const bool isChanged = m_currentPanelId != panelId;
    m_currentPanelId = panelId;
    updateHeader();
    if (isChanged)
    {
        Q_EMIT currentPanelChanged(panelId);
    }
    return true;
}

PDFAppDocumentPanel* PDFAppDocumentSidebar::createPanel(const QString& panelId)
{
    QPointer<PDFAppDocumentPanel>& existing = m_panels[panelId];
    if (existing)
    {
        return existing;
    }

    const PDFAppDocumentPanelFactory* factory = m_registry.findFactory(panelId);
    if (!factory || !m_stack)
    {
        return nullptr;
    }

    QPointer<PDFAppDocumentSidebar> self(this);
    QPointer<PDFAppDocumentPanel> panel = factory->createPanel(m_stack);
    if (!self || !panel)
    {
        return nullptr;
    }

    m_panels[panelId] = panel;
    m_stack->addWidget(panel);
    connect(panel, &PDFAppDocumentPanel::titleChanged, this, &PDFAppDocumentSidebar::onPanelTitleChanged);
    connect(panel, &PDFAppDocumentPanel::availabilityChanged, this, &PDFAppDocumentSidebar::onPanelAvailabilityChanged);
    connect(panel, &QObject::destroyed, this, &PDFAppDocumentSidebar::onPanelDestroyed);
    panel->setDocument(m_document, m_view);
    if (!self || !panel)
    {
        return nullptr;
    }

    if (m_header)
    {
        QPointer<QWidget> headerWidget = panel->createHeaderWidget(m_header);
        if (!self || !panel)
        {
            return nullptr;
        }

        if (headerWidget)
        {
            headerWidget->setParent(m_header);
            headerWidget->hide();
            m_headerLayout->addWidget(headerWidget, 0, Qt::AlignVCenter);
            m_headerWidgets[panelId] = headerWidget;
        }
    }

    return panel;
}

QString PDFAppDocumentSidebar::getCurrentPanelId() const
{
    return m_currentPanelId;
}

PDFAppDocumentPanel* PDFAppDocumentSidebar::findPanel(const QString& panelId) const
{
    auto it = m_panels.find(panelId);
    return it != m_panels.end() ? it->second.data() : nullptr;
}

std::vector<PDFInteger> PDFAppDocumentSidebar::getSelectedPages() const
{
    const PDFAppThumbnailsPanel* thumbnails = qobject_cast<const PDFAppThumbnailsPanel*>(findPanel(PDFAppDocumentPanelIds::thumbnails()));
    return thumbnails ? thumbnails->getSelectedPages() : std::vector<PDFInteger>();
}

QToolButton* PDFAppDocumentSidebar::getTabButton(const QString& panelId) const
{
    for (const TabInfo& tab : m_tabs)
    {
        if (tab.panelId == panelId)
        {
            return tab.button.data();
        }
    }
    return nullptr;
}

bool PDFAppDocumentSidebar::isTabVisible(const QString& panelId) const
{
    const QToolButton* button = getTabButton(panelId);
    return button && !button->isHidden();
}

QWidget* PDFAppDocumentSidebar::getHeaderWidget(const QString& panelId) const
{
    auto it = m_headerWidgets.find(panelId);
    return it != m_headerWidgets.end() ? it->second.data() : nullptr;
}

QWidget* PDFAppDocumentSidebar::getHeader() const
{
    return m_header.data();
}

QString PDFAppDocumentSidebar::getHeaderTitle() const
{
    return m_headerTitle ? m_headerTitle->text() : QString();
}

QString PDFAppDocumentSidebar::getHeaderSubtitle() const
{
    return m_headerSubtitle ? m_headerSubtitle->text() : QString();
}

void PDFAppDocumentSidebar::onTabClicked(int buttonId)
{
    if (buttonId < 0 || buttonId >= int(m_tabs.size()))
    {
        return;
    }

    const QString panelId = m_tabs[buttonId].panelId;
    showPanel(panelId);
}

void PDFAppDocumentSidebar::onPanelTitleChanged()
{
    updateHeader();
}

void PDFAppDocumentSidebar::onPanelAvailabilityChanged()
{
    updateTabs();
}

void PDFAppDocumentSidebar::onPanelDestroyed(QObject* object)
{
    // The panel is not whole any more: only the pointers are forgotten
    for (auto it = m_panels.begin(); it != m_panels.end(); ++it)
    {
        if (it->second.isNull() || static_cast<QObject*>(it->second.data()) == object)
        {
            auto headerIt = m_headerWidgets.find(it->first);
            if (headerIt != m_headerWidgets.end())
            {
                if (headerIt->second)
                {
                    headerIt->second->deleteLater();
                }
                m_headerWidgets.erase(headerIt);
            }
        }
    }

    for (auto it = m_panels.begin(); it != m_panels.end();)
    {
        if (it->second.isNull() || static_cast<QObject*>(it->second.data()) == object)
        {
            it = m_panels.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void PDFAppDocumentSidebar::onDocumentStateChanged()
{
    updateTabs();

    if (!m_document || !m_document->isReady())
    {
        return;
    }

    // The panel requested before the document was ready, otherwise the shown or
    // the default panel
    QString panelId = m_requestedPanelId;
    m_requestedPanelId.clear();
    if (panelId.isEmpty() || !isPanelAvailable(panelId))
    {
        panelId = m_currentPanelId;
    }
    if (panelId.isEmpty() || !isPanelAvailable(panelId))
    {
        showFirstAvailablePanel();
    }
    else
    {
        showPanel(panelId);
    }
}

void PDFAppDocumentSidebar::onDocumentContentChanged()
{
    updateTabs();
    if (m_document && m_document->isReady() && !m_currentPanelId.isEmpty() && !isPanelAvailable(m_currentPanelId))
    {
        showFirstAvailablePanel();
    }
}

void PDFAppDocumentSidebar::onDocumentDestroyed()
{
    m_document = nullptr;
    updateTabs();
}

void PDFAppDocumentSidebar::onSettingChanged(const QString& topic, const QString& key)
{
    if (topic == PDFSettingsRepository::getTopic() && key == PDFSettingKeys::viewingSidebarIconSize())
    {
        updateTabSizes();
    }
}

void PDFAppDocumentSidebar::onEverythingChanged()
{
    updateTabSizes();
}

void PDFAppDocumentSidebar::showFirstAvailablePanel()
{
    const QString defaultPanel = getDefaultPanelId();
    if (isPanelAvailable(defaultPanel) && showPanel(defaultPanel))
    {
        return;
    }

    for (const TabInfo& tab : m_tabs)
    {
        if (isPanelAvailable(tab.panelId) && showPanel(tab.panelId))
        {
            return;
        }
    }
}

QString PDFAppDocumentSidebar::getDefaultPanelId() const
{
    return PDFAppDocumentPanelIds::thumbnails();
}

bool PDFAppDocumentSidebar::isPanelAvailable(const QString& panelId) const
{
    const PDFAppDocumentPanelFactory* factory = m_registry.findFactory(panelId);
    if (!factory)
    {
        return false;
    }

    // Before the document is ready, nothing is known about its content
    if (!m_document || !m_document->isReady())
    {
        return true;
    }
    return factory->isAvailable(m_document);
}

void PDFAppDocumentSidebar::updateTabs()
{
    const bool isReady = m_document && m_document->isReady();
    for (const TabInfo& tab : m_tabs)
    {
        if (tab.button)
        {
            // Tabs are shown only for a ready document, which has the content
            tab.button->setVisible(isReady && tab.factory->isAvailable(m_document));
        }
    }
}

void PDFAppDocumentSidebar::updateTabSizes()
{
    PDFAppContext* context = m_view ? m_view->getContext() : nullptr;
    const QString value = context ? context->getSettings().getString(PDFSettingKeys::viewingSidebarIconSize()) : QString();

    int tabSize = 32;
    int iconSize = 18;
    PDFAppDocumentSidebarHelper::getTabSize(value, &tabSize, &iconSize);
    for (const TabInfo& tab : m_tabs)
    {
        if (tab.button)
        {
            tab.button->setFixedSize(tabSize, tabSize);
            tab.button->setIconSize(QSize(iconSize, iconSize));
        }
    }
}

void PDFAppDocumentSidebar::updateTabTexts()
{
    for (const TabInfo& tab : m_tabs)
    {
        if (!tab.button)
        {
            continue;
        }

        QString title = tab.factory->getTitle();
        QString iconName = tab.factory->getIconName();
        if (title.isEmpty() || iconName.isEmpty())
        {
            // The factory does not know them: the panel tells them
            const PDFAppDocumentPanel* panel = findPanel(tab.panelId);
            if (!panel)
            {
                panel = createPanel(tab.panelId);
            }
            if (panel)
            {
                title = title.isEmpty() ? panel->getTitle() : title;
                iconName = iconName.isEmpty() ? panel->getIconName() : iconName;
            }
        }

        tab.button->setToolTip(title);
        tab.button->setAccessibleName(title);
        if (tab.button->property("pdf4qtIconName").toString() != iconName)
        {
            tab.button->setProperty("pdf4qtIconName", iconName);
            tab.button->setIcon(PDFThemeIcons::getIcon(iconName));
        }
    }
}

void PDFAppDocumentSidebar::updateHeader()
{
    for (const auto& item : m_headerWidgets)
    {
        if (item.second)
        {
            item.second->setVisible(item.first == m_currentPanelId);
        }
    }

    const PDFAppDocumentPanel* panel = findPanel(m_currentPanelId);
    if (m_header)
    {
        m_header->setVisible(!panel || !panel->hasOwnHeader());
    }
    if (m_headerTitle)
    {
        m_headerTitle->setHeaderText(panel ? panel->getTitle() : QString());
    }
    if (m_headerSubtitle)
    {
        const QString subtitle = panel ? panel->getSubtitle() : QString();
        m_headerSubtitle->setHeaderText(subtitle);
        m_headerSubtitle->setVisible(!subtitle.isEmpty());
    }
}

void PDFAppDocumentSidebar::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        updateTabTexts();
        updateHeader();
    }
}

void PDFAppDocumentSidebar::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // bg/sidebar with the border/default line at the trailing edge (sidebar 193:883)
    QPainter painter(this);
    const PDFThemeManager* manager = PDFThemeManager::getInstance();
    painter.fillRect(rect(), manager->getColor(PDFThemeColors::BgSidebar));
    const int x = layoutDirection() == Qt::RightToLeft ? 0 : width() - 1;
    painter.fillRect(QRect(x, 0, 1, height()), manager->getColor(PDFThemeColors::BorderDefault));
}

}   // namespace pdf
