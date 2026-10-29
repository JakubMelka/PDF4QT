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

#include "pdfappdocumenttoolbar.h"
#include "pdfappannotatepanels.h"
#include "pdfappannotationcontroller.h"
#include "pdfapplocale.h"
#include "pdfappcommand.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentmenus.h"
#include "pdfappdocumenttoolspanel.h"
#include "pdfappdocumentview.h"
#include "pdfappmainwindow.h"
#include "pdfdatabasenotifier.h"
#include "pdfshortcutrepository.h"

#include "pdfwidgettool.h"

#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QApplication>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>

#include <algorithm>
#include <cmath>

namespace pdf
{

/// Helpers of the toolbar
class PDFAppDocumentToolbarHelper
{
public:
    PDFAppDocumentToolbarHelper() = delete;

    /// Property of a button: the id of its command
    static constexpr const char* COMMAND_PROPERTY = "pdf4qtCommandId";

    /// Levels of the narrowing of the toolbar. Annotate and Insert are the only
    /// entries of their panels (step 4), so they are hidden last: the page
    /// navigation has the page item of the status bar and the keys.
    enum CollapseLevel
    {
        Full = 0,
        MenuIconsOnly,
        SearchNarrow,
        ZoomHidden,
        SearchMinimum,
        PageNavigationHidden,
        AnnotateInsertHidden,
        LastLevel = AnnotateInsertHidden
    };

    /// Creates a horizontal group without margins
    static QHBoxLayout* createGroup(QWidget* group, int spacing)
    {
        QHBoxLayout* layout = new QHBoxLayout(group);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(spacing);
        return layout;
    }

    /// Icon of the tool of the canvas shown by the Select button
    static QString getSelectIconName(const PDFAppDocumentView* view)
    {
        if (view && view->isCommandChecked(PDFAppCommands::selectTable()))
        {
            return QStringLiteral("table");
        }
        if (view && view->isCommandChecked(PDFAppCommands::screenshot()))
        {
            return QStringLiteral("camera");
        }
        if (view && view->isCommandChecked(PDFAppCommands::extractImage()))
        {
            return QStringLiteral("image");
        }
        if (view && view->isCommandChecked(PDFAppCommands::handTool()))
        {
            // The pointer of the library stands for the hand, until "hand" exists in it
            const QString handIcon = PDFAppCommands::getHandIconName();
            return handIcon.isEmpty() ? QStringLiteral("cursor") : handIcon;
        }
        return QStringLiteral("text-cursor");
    }
};

// ------------------------------------------------------------ PDFAppToolbarDivider

PDFAppToolbarDivider::PDFAppToolbarDivider(Qt::Orientation orientation, int length, int padding, PDFThemeColors::Color color, QWidget* parent) :
    BaseClass(parent),
    m_orientation(orientation),
    m_length(qMax(0, length)),
    m_padding(qMax(0, padding)),
    m_color(color)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setFocusPolicy(Qt::NoFocus);
    if (orientation == Qt::Horizontal)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    else
    {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppToolbarDivider::update));
}

QSize PDFAppToolbarDivider::sizeHint() const
{
    if (m_orientation == Qt::Horizontal)
    {
        return QSize(m_length + 2 * m_padding, 1);
    }
    return QSize(1 + 2 * m_padding, m_length);
}

QSize PDFAppToolbarDivider::minimumSizeHint() const
{
    return sizeHint();
}

void PDFAppToolbarDivider::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    const QColor color = PDFThemeManager::getInstance()->getColor(m_color);
    if (m_orientation == Qt::Horizontal)
    {
        painter.fillRect(QRect(m_padding, (height() - 1) / 2, qMax(0, width() - 2 * m_padding), 1), color);
    }
    else
    {
        const int length = m_length > 0 ? qMin(m_length, height()) : height();
        painter.fillRect(QRect(m_padding, (height() - length) / 2, 1, length), color);
    }
}

// ----------------------------------------------------------- PDFAppDocumentToolbar

PDFAppDocumentToolbar::PDFAppDocumentToolbar(PDFAppDocumentView* view, QWidget* parent) :
    BaseClass(parent),
    m_view(view)
{
    using C = PDFAppCommands;
    using H = PDFAppDocumentToolbarHelper;

    setObjectName(QStringLiteral("documentToolbar"));
    setFixedHeight(HEIGHT);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

    // toolbar 193:785: paddings 12, gap 4 between the groups
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(12, 0, 12, 0);
    m_layout->setSpacing(4);

    // toolbar-left 193:786: buttons 2 px apart, dividers 13 x 20
    m_leftGroup = new QWidget(this);
    m_leftGroup->setObjectName(QStringLiteral("toolbarLeft"));
    QHBoxLayout* leftLayout = H::createGroup(m_leftGroup, 2);
    leftLayout->addWidget(createCommandButton(C::toggleSidebar(), QStringLiteral("panel-left")));
    leftLayout->addWidget(new PDFAppToolbarDivider(Qt::Vertical, 20, 6, PDFThemeColors::BorderSeparator, m_leftGroup));
    leftLayout->addWidget(createCommandButton(C::save(), QStringLiteral("save")));
    leftLayout->addWidget(createCommandButton(C::undo(), QStringLiteral("undo")));
    leftLayout->addWidget(createCommandButton(C::redo(), QStringLiteral("redo")));
    leftLayout->addWidget(new PDFAppToolbarDivider(Qt::Vertical, 20, 6, PDFThemeColors::BorderSeparator, m_leftGroup));
    m_fileButton = createMenuButton(QStringLiteral("menuFile"), QStringLiteral("file"));
    m_viewButton = createMenuButton(QStringLiteral("menuView"), QStringLiteral("eye"));
    m_annotateButton = createMenuButton(QStringLiteral("menuAnnotate"), QStringLiteral("highlighter"));
    m_insertButton = createMenuButton(QStringLiteral("menuInsert"), QStringLiteral("plus-circle"));
    m_goToButton = createMenuButton(QStringLiteral("menuGoTo"), QStringLiteral("compass"));
    m_toolsButton = createMenuButton(QStringLiteral("menuTools"), QStringLiteral("tools"));
    for (QToolButton* button : { m_fileButton.data(), m_viewButton.data(), m_annotateButton.data(), m_insertButton.data(), m_goToButton.data(), m_toolsButton.data() })
    {
        leftLayout->addWidget(button);
    }

    // Annotate and Insert open the panels of step 4 (enabled by their commands -
    // a locked document or one without the permission disables them); Tools opens
    // the Tools panel of step 6 (196:3650)
    m_annotateButton->setEnabled(false);
    m_insertButton->setEnabled(false);
    m_layout->addWidget(m_leftGroup);
    m_layout->addStretch(1);

    // toolbar-center 193:840: the page navigation and the zoom, 12 px apart
    QWidget* centerGroup = new QWidget(this);
    centerGroup->setObjectName(QStringLiteral("toolbarCenter"));
    QHBoxLayout* centerLayout = H::createGroup(centerGroup, 12);

    m_pageGroup = new QWidget(centerGroup);
    m_pageGroup->setObjectName(QStringLiteral("pageNavigation"));
    QHBoxLayout* pageLayout = H::createGroup(m_pageGroup, 2);
    pageLayout->addWidget(createCommandButton(C::previousPage(), QStringLiteral("chevron-left")));
    m_pageField = new PDFToolBarField(m_pageGroup);
    m_pageField->setObjectName(QStringLiteral("pageField"));
    m_pageField->setMaxLength(256);
    connect(m_pageField.data(), &QLineEdit::returnPressed, this, &PDFAppDocumentToolbar::onPageFieldEdited);
    connect(m_pageField.data(), &QLineEdit::editingFinished, this, &PDFAppDocumentToolbar::updatePageField);
    m_pageField->installEventFilter(this);
    pageLayout->addWidget(m_pageField);
    pageLayout->addWidget(createCommandButton(C::nextPage(), QStringLiteral("chevron-right")));
    centerLayout->addWidget(m_pageGroup);

    m_zoomGroup = new QWidget(centerGroup);
    m_zoomGroup->setObjectName(QStringLiteral("zoom"));
    QHBoxLayout* zoomLayout = H::createGroup(m_zoomGroup, 2);
    zoomLayout->addWidget(createCommandButton(C::zoomOut(), QStringLiteral("minus")));
    m_zoomField = new PDFToolBarField(m_zoomGroup);
    m_zoomField->setObjectName(QStringLiteral("zoomField"));
    m_zoomField->setSuffixVisible(false);
    m_zoomField->setMaxLength(16);
    connect(m_zoomField.data(), &QLineEdit::returnPressed, this, &PDFAppDocumentToolbar::onZoomFieldEdited);
    connect(m_zoomField.data(), &QLineEdit::editingFinished, this, &PDFAppDocumentToolbar::updateZoomField);
    m_zoomField->installEventFilter(this);
    zoomLayout->addWidget(m_zoomField);
    zoomLayout->addWidget(createCommandButton(C::zoomIn(), QStringLiteral("plus")));
    centerLayout->addWidget(m_zoomGroup);
    m_layout->addWidget(centerGroup);
    m_layout->addStretch(1);

    // toolbar-right 193:870: the Select button with its menu and the search field, 8 px apart
    m_rightGroup = new QWidget(this);
    m_rightGroup->setObjectName(QStringLiteral("toolbarRight"));
    QHBoxLayout* rightLayout = H::createGroup(m_rightGroup, 8);
    m_selectButton = createButton(QStringLiteral("selectButton"), QStringLiteral("text-cursor"));
    m_selectMenu = new PDFAppDocumentSelectMenu(view, m_selectButton);
    m_selectButton->setMenu(m_selectMenu);
    m_selectButton->setPopupMode(QToolButton::InstantPopup);

    // The Select menu is a panel of the menu bar: the pointer over another panel
    // button and Left / Right switch to its panel (eventFilter)
    m_selectMenu->installEventFilter(this);
    connect(m_selectMenu.data(), &QMenu::aboutToHide, this, &PDFAppDocumentToolbar::onSelectMenuAboutToHide);
    rightLayout->addWidget(m_selectButton);

    // The search of package D: the field only, D connects its behavior
    m_searchField = new PDFLineEdit(m_rightGroup);
    m_searchField->setObjectName(QStringLiteral("documentSearchField"));
    m_searchField->setSearchField();
    m_searchField->setFixedWidth(SEARCH_FIELD_WIDTH);
    m_searchField->setMaxLength(1024);
    rightLayout->addWidget(m_searchField);
    m_layout->addWidget(m_rightGroup);

    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppDocumentToolbar::update));
    if (view)
    {
        connect(view, &PDFAppDocumentView::commandStatesChanged, this, &PDFAppDocumentToolbar::updateState);
        connect(view, &PDFAppDocumentView::viewChanged, this, &PDFAppDocumentToolbar::onViewChanged);
        connect(view, &PDFAppDocumentView::displayedPageChanged, this, &PDFAppDocumentToolbar::onViewChanged);

        // The tooltips show the shortcuts of the user: the view keeps them and it
        // updates the tooltips after a notification of the database
    }

    retranslate();
    updateState();
}

PDFAppDocumentToolbar::~PDFAppDocumentToolbar()
{
    // The panels close while the toolbar is whole (they are popup windows); their
    // signals reach the dying toolbar no more (the buttons would emit toggled to
    // the application while the tab is being destroyed)
    for (const auto& item : m_panels)
    {
        if (PDFAppDocumentPanelPopup* panel = item.second.data())
        {
            panel->disconnect(this);
            delete panel;
        }
    }
    m_panels.clear();
}

QToolButton* PDFAppDocumentToolbar::createButton(const QString& objectName, const QString& iconName)
{
    // Toolbar button 190:538: 36 x 36, icon 20, no background at rest
    QToolButton* button = new QToolButton(this);
    button->setObjectName(objectName);
    button->setAutoRaise(true);
    button->setIcon(PDFThemeIcons::getIcon(iconName));
    button->setIconSize(QSize(20, 20));
    button->setFocusPolicy(Qt::TabFocus);
    return button;
}

QToolButton* PDFAppDocumentToolbar::createCommandButton(const QString& commandId, const QString& iconName)
{
    QToolButton* button = createButton(commandId, iconName);
    button->setProperty(PDFAppDocumentToolbarHelper::COMMAND_PROPERTY, commandId);
    connect(button, &QToolButton::clicked, this, &PDFAppDocumentToolbar::onCommandButtonClicked);
    m_commandButtons[commandId] = button;
    return button;
}

QToolButton* PDFAppDocumentToolbar::createMenuButton(const QString& objectName, const QString& iconName)
{
    // Toolbar menu button 190:577: icon 18 and the label Medium 13; Open = checked
    QToolButton* button = createButton(objectName, iconName);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setIconSize(QSize(18, 18));
    button->setCheckable(true);
    connect(button, &QToolButton::clicked, this, &PDFAppDocumentToolbar::onMenuButtonClicked);
    m_menuButtons.emplace_back(button);
    return button;
}

void PDFAppDocumentToolbar::retranslate()
{
    const std::pair<QToolButton*, QString> menuTexts[] =
    {
        { m_fileButton.data(), tr("File") },
        { m_viewButton.data(), tr("View") },
        { m_annotateButton.data(), tr("Annotate") },
        { m_insertButton.data(), tr("Insert") },
        { m_goToButton.data(), tr("Go to") },
        { m_toolsButton.data(), tr("Tools") }
    };
    for (const std::pair<QToolButton*, QString>& item : menuTexts)
    {
        if (item.first)
        {
            item.first->setText(item.second);
            item.first->setToolTip(item.second);
            item.first->setAccessibleName(item.second);
        }
    }

    updateCommandToolTips();

    if (m_selectButton)
    {
        m_selectButton->setToolTip(tr("Select"));
        m_selectButton->setAccessibleName(tr("Select"));
    }
    if (m_searchField)
    {
        m_searchField->setPlaceholderText(tr("Search in document…"));
        m_searchField->setAccessibleName(tr("Search in document"));
    }
    if (m_pageField)
    {
        m_pageField->setAccessibleName(tr("Page"));
    }
    if (m_zoomField)
    {
        m_zoomField->setAccessibleName(tr("Zoom"));
    }

    updatePageField();
    updateZoomField();
}

void PDFAppDocumentToolbar::updateCommandToolTips()
{
    // Icon buttons: the title of the command and the shortcut of the user
    const PDFAppDocumentView* view = m_view.data();
    PDFAppContext* context = view ? view->getContext() : nullptr;
    const PDFAppCommandRegistry* registry = context ? context->getCommandRegistry() : nullptr;
    for (const auto& item : m_commandButtons)
    {
        QToolButton* button = item.second.data();
        const PDFAppCommand* command = registry ? registry->findCommand(item.first) : nullptr;
        if (!button || !command)
        {
            continue;
        }

        button->setAccessibleName(command->getTitle());
        button->setToolTip(view->getCommandToolTip(item.first, false));
    }
}

PDFAppDocumentView* PDFAppDocumentToolbar::getView() const
{
    return m_view.data();
}

PDFLineEdit* PDFAppDocumentToolbar::getSearchField() const
{
    return m_searchField.data();
}

PDFToolBarField* PDFAppDocumentToolbar::getPageField() const
{
    return m_pageField.data();
}

PDFToolBarField* PDFAppDocumentToolbar::getZoomField() const
{
    return m_zoomField.data();
}

QToolButton* PDFAppDocumentToolbar::getCommandButton(const QString& commandId) const
{
    auto it = m_commandButtons.find(commandId);
    return it != m_commandButtons.end() ? it->second.data() : nullptr;
}

QToolButton* PDFAppDocumentToolbar::getMenuButton(Panel panel) const
{
    switch (panel)
    {
        case Panel::File:
            return m_fileButton.data();
        case Panel::View:
            return m_viewButton.data();
        case Panel::GoTo:
            return m_goToButton.data();
        case Panel::Annotate:
            return m_annotateButton.data();
        case Panel::Insert:
            return m_insertButton.data();
        case Panel::Tools:
            return m_toolsButton.data();
    }
    return nullptr;
}

QToolButton* PDFAppDocumentToolbar::getAnnotateButton() const
{
    return m_annotateButton.data();
}

QToolButton* PDFAppDocumentToolbar::getInsertButton() const
{
    return m_insertButton.data();
}

QToolButton* PDFAppDocumentToolbar::getToolsButton() const
{
    return m_toolsButton.data();
}

QToolButton* PDFAppDocumentToolbar::getSelectButton() const
{
    return m_selectButton.data();
}

PDFAppDocumentSelectMenu* PDFAppDocumentToolbar::getSelectMenu() const
{
    return m_selectMenu.data();
}

PDFAppDocumentPanelPopup* PDFAppDocumentToolbar::findPanel(Panel panel) const
{
    auto it = m_panels.find(panel);
    return it != m_panels.end() ? it->second.data() : nullptr;
}

PDFAppDocumentPanelPopup* PDFAppDocumentToolbar::getOpenPanel() const
{
    for (const auto& item : m_panels)
    {
        if (item.second && item.second->isVisible())
        {
            return item.second.data();
        }
    }
    return nullptr;
}

PDFAppDocumentPanelPopup* PDFAppDocumentToolbar::openPanel(Panel panel)
{
    QToolButton* button = getMenuButton(panel);
    if (!button || !button->isVisible() || !button->isEnabled() || !isVisible())
    {
        return nullptr;
    }

    // One panel at a time; the button of the closed panel is not checked at once
    // (its signal closed comes from the event loop, the switch of the menu bar
    // shows one checked button)
    // A slot of a button (toggled) can delete the tab with the toolbar
    QPointer<PDFAppDocumentToolbar> toolbarGuard(this);
    QPointer<QToolButton> buttonGuard(button);
    if (PDFAppDocumentPanelPopup* openPanel = getOpenPanel())
    {
        QPointer<QToolButton> openButton = qobject_cast<QToolButton*>(openPanel->getAnchor());
        openPanel->close();
        if (openButton && openButton != button)
        {
            openButton->setChecked(false);
        }
        if (!toolbarGuard || !buttonGuard)
        {
            return nullptr;
        }
    }

    QPointer<PDFAppDocumentPanelPopup>& popup = m_panels[panel];
    if (!popup)
    {
        switch (panel)
        {
            case Panel::File:
                popup = new PDFAppDocumentFilePanel(m_view, this);
                break;
            case Panel::View:
                popup = new PDFAppDocumentViewPanel(m_view, this);
                break;
            case Panel::GoTo:
                popup = new PDFAppDocumentGoToPanel(m_view, this);
                break;
            case Panel::Annotate:
                popup = new PDFAppAnnotatePanel(m_view, this);
                break;
            case Panel::Insert:
                popup = new PDFAppInsertPanel(m_view, this);
                break;
            case Panel::Tools:
                popup = new PDFAppDocumentToolsPanel(m_view, this);
                break;
        }
        connect(popup.data(), &PDFAppDocumentPanelPopup::closed, this, &PDFAppDocumentToolbar::onPanelClosed);

        // A panel deleted by the application while it is open sends no closed: its
        // button is unchecked too (the slot does not touch the deleted panel)
        connect(popup.data(), &QObject::destroyed, this, &PDFAppDocumentToolbar::onPanelDestroyed);
        connect(popup.data(), &PDFAppDocumentPanelPopup::neighborRequested, this, &PDFAppDocumentToolbar::onPanelNeighborRequested);

        // The panel grabs the mouse: the pointer over another panel button is seen
        // by the moves of the panel (eventFilter)
        popup->setMouseTracking(true);
        popup->installEventFilter(this);
    }

    // The panel opens 4 px below the toolbar (y 94 of the frames)
    QPointer<PDFAppDocumentPanelPopup> guard(popup.data());
    button->setChecked(true);
    if (!toolbarGuard || !guard || !buttonGuard)
    {
        return nullptr;
    }
    guard->popup(button, mapToGlobal(QPoint(0, height())).y() + PANEL_GAP);
    if ((!guard || !guard->isVisible()) && buttonGuard)
    {
        buttonGuard->setChecked(false);
    }
    return guard.data();
}

void PDFAppDocumentToolbar::onMenuButtonClicked()
{
    // The menu buttons do not identify themselves in the signal
    QToolButton* button = qobject_cast<QToolButton*>(sender());
    if (!button)
    {
        return;
    }

    for (Panel panel : { Panel::File, Panel::View, Panel::GoTo, Panel::Annotate, Panel::Insert, Panel::Tools })
    {
        if (getMenuButton(panel) != button)
        {
            continue;
        }

        PDFAppDocumentPanelPopup* popup = findPanel(panel);
        if (popup && popup->isVisible())
        {
            popup->close();
            button->setChecked(false);
        }
        else
        {
            openPanel(panel);
        }
        return;
    }
}

void PDFAppDocumentToolbar::onPanelDestroyed(QObject* object)
{
    // ~QWidget emits destroyed before the QPointer of the panel is cleared: the
    // dying panel is only compared, none of its members is called (CLAUDE.md)
    for (const auto& item : m_panels)
    {
        if (item.second.data() == object)
        {
            if (QToolButton* button = getMenuButton(item.first))
            {
                button->setChecked(false);
            }
        }
    }
}

void PDFAppDocumentToolbar::onPanelClosed()
{
    // The buttons of the closed panels are not checked
    for (Panel panel : { Panel::File, Panel::View, Panel::GoTo, Panel::Annotate, Panel::Insert, Panel::Tools })
    {
        QToolButton* button = getMenuButton(panel);
        PDFAppDocumentPanelPopup* popup = findPanel(panel);
        if (button)
        {
            button->setChecked(popup && popup->isVisible());
        }
    }
}

std::vector<QToolButton*> PDFAppDocumentToolbar::getPanelButtons() const
{
    // The order of the layout
    std::vector<QToolButton*> buttons;
    for (QToolButton* button : { m_fileButton.data(), m_viewButton.data(), m_annotateButton.data(), m_insertButton.data(), m_goToButton.data(), m_toolsButton.data(), m_selectButton.data() })
    {
        if (button)
        {
            buttons.push_back(button);
        }
    }
    return buttons;
}

QToolButton* PDFAppDocumentToolbar::getOpenPanelButton() const
{
    if (m_selectMenu && m_selectMenu->isVisible())
    {
        return m_selectButton.data();
    }

    for (Panel panel : { Panel::File, Panel::View, Panel::GoTo, Panel::Annotate, Panel::Insert, Panel::Tools })
    {
        const PDFAppDocumentPanelPopup* popup = findPanel(panel);
        if (popup && popup->isVisible())
        {
            return getMenuButton(panel);
        }
    }
    return nullptr;
}

bool PDFAppDocumentToolbar::switchPanel(QToolButton* button, bool byKeyboard)
{
    QToolButton* openButton = getOpenPanelButton();
    if (!button || !openButton || button == openButton || !isVisible() || !button->isVisible() || !button->isEnabled())
    {
        return false;
    }

    // A modal window (a dialog opened from a panel) blocks the toolbar
    const QWidget* modalWidget = QApplication::activeModalWidget();
    if (modalWidget && modalWidget != window() && !modalWidget->isAncestorOf(this))
    {
        return false;
    }

    const std::vector<QToolButton*> buttons = getPanelButtons();
    if (std::find(buttons.cbegin(), buttons.cend(), button) == buttons.cend())
    {
        return false;
    }

    // The open panel hides and the new one shows in one step, no window is
    // animated (the style turns the effects of the popups off): nothing flickers
    QPointer<PDFAppDocumentToolbar> guard(this);
    QPointer<QToolButton> buttonGuard(button);
    if (button == m_selectButton)
    {
        QPointer<QToolButton> openButtonGuard(openButton);
        if (PDFAppDocumentPanelPopup* openPanel = getOpenPanel())
        {
            openPanel->close();
        }
        if (guard && openButtonGuard)
        {
            // The button of the closed panel is not checked at once (one marked button)
            openButtonGuard->setChecked(false);
        }
        if (!guard || !buttonGuard)
        {
            // A slot of a button has deleted the toolbar
            return false;
        }
        openSelectMenu(byKeyboard);
        return guard && m_selectMenu && m_selectMenu->isVisible();
    }

    if (m_selectMenu && m_selectMenu->isVisible())
    {
        m_selectMenu->hide();
        if (!guard || !buttonGuard)
        {
            return false;
        }
    }

    for (Panel panel : { Panel::File, Panel::View, Panel::GoTo, Panel::Annotate, Panel::Insert, Panel::Tools })
    {
        if (getMenuButton(panel) == button)
        {
            // openPanel closes the open panel itself
            return openPanel(panel) != nullptr;
        }
    }
    return false;
}

void PDFAppDocumentToolbar::openSelectMenu(bool byKeyboard)
{
    QToolButton* button = m_selectButton.data();
    QPointer<PDFAppDocumentSelectMenu> menu = m_selectMenu.data();
    if (!button || !menu || !button->isVisible() || !button->isEnabled())
    {
        return;
    }

    // The menu opens as by its button (below it, the leading edges aligned; the
    // menu moves itself 4 px below the toolbar), but without the nested event loop
    // of QToolButton::showMenu: the switch runs inside of an event of the closed
    // panel. A click on the button closes the menu and does not open it again.
    const QMargins margins = PDFWidgetStyle::getPopoverMargins(menu);
    const int menuWidth = qMax(menu->sizeHint().width(), PDFAppDocumentSelectMenu::PANEL_WIDTH + margins.left() + margins.right());
    QPoint position = button->mapToGlobal(QPoint(0, button->height()));
    if (button->isRightToLeft())
    {
        position.rx() += button->width() - menuWidth;
    }

    menu->setNoReplayFor(button);
    button->setDown(true);
    menu->popup(position);
    if (!menu || !menu->isVisible())
    {
        return;
    }

    if (byKeyboard)
    {
        // The keyboard starts at the current tool (else the first enabled item)
        QAction* current = nullptr;
        const QList<QAction*> actions = menu->actions();
        for (QAction* action : actions)
        {
            if (!action->isEnabled() || action->isSeparator())
            {
                continue;
            }
            if (!current || (action->isChecked() && !current->isChecked()))
            {
                current = action;
            }
        }
        if (current)
        {
            menu->setActiveAction(current);
        }
    }
}

void PDFAppDocumentToolbar::onSelectMenuAboutToHide()
{
    // The button opened the menu down (also when the menu was opened by switchPanel)
    if (m_selectButton && m_selectButton->isDown())
    {
        m_selectButton->setDown(false);
    }
}

void PDFAppDocumentToolbar::onPanelNeighborRequested(bool isNext)
{
    switchToNeighbor(isNext);
}

void PDFAppDocumentToolbar::switchToNeighbor(bool isNext)
{
    // The neighboring visible and enabled panel button, around the ends (menu bar)
    const std::vector<QToolButton*> buttons = getPanelButtons();
    const auto it = std::find(buttons.cbegin(), buttons.cend(), getOpenPanelButton());
    if (it == buttons.cend())
    {
        return;
    }

    const int count = int(buttons.size());
    const int index = int(std::distance(buttons.cbegin(), it));
    for (int step = 1; step < count; ++step)
    {
        QToolButton* button = buttons[(index + (isNext ? step : count - step)) % count];
        if (button->isVisible() && button->isEnabled())
        {
            switchPanel(button, true);
            return;
        }
    }
}

QToolButton* PDFAppDocumentToolbar::findPanelButtonAt(const QPoint& globalPosition) const
{
    for (QToolButton* button : getPanelButtons())
    {
        if (button->isVisible() && QRect(button->mapToGlobal(QPoint(0, 0)), button->size()).contains(globalPosition))
        {
            return button;
        }
    }
    return nullptr;
}

bool PDFAppDocumentToolbar::isPanelWindow(const QObject* object) const
{
    if (!object)
    {
        return false;
    }
    if (object == m_selectMenu)
    {
        return true;
    }

    for (const auto& item : m_panels)
    {
        if (item.second == object)
        {
            return true;
        }
    }
    return false;
}

void PDFAppDocumentToolbar::onCommandButtonClicked()
{
    // The buttons do not identify themselves in the signal; the command runs from
    // the event loop (it may delete the tab together with the toolbar)
    QToolButton* button = qobject_cast<QToolButton*>(sender());
    if (button && m_view)
    {
        m_view->requestCommand(button->property(PDFAppDocumentToolbarHelper::COMMAND_PROPERTY).toString());
    }
}

void PDFAppDocumentToolbar::updateState()
{
    PDFAppDocumentView* view = m_view.data();
    for (const auto& item : m_commandButtons)
    {
        if (QToolButton* button = item.second.data())
        {
            button->setEnabled(view && view->isCommandEnabled(item.first));
        }
    }

    // The panels and the tools need a shown document
    const PDFAppDocument* document = view ? view->getDocument() : nullptr;
    const bool isReady = document && document->isReady();

    // Annotate and Insert follow their commands (O20: a locked document or one
    // without the permission disables them, the tooltip says why)
    for (const auto& item : { std::make_pair(m_annotateButton.data(), PDFAppCommands::annotatePanel()),
                              std::make_pair(m_insertButton.data(), PDFAppCommands::insertPanel()) })
    {
        if (!item.first)
        {
            continue;
        }

        const bool isEnabled = view && view->isCommandEnabled(item.second);
        item.first->setEnabled(isEnabled);
        const PDFAppAnnotationController* controller = view ? view->getAnnotationController() : nullptr;
        const QString reason = (!isEnabled && controller && isReady) ? controller->getEditingRefusalReason() : QString();
        item.first->setToolTip(reason.isEmpty() ? item.first->accessibleName() : reason);
    }
    for (QToolButton* button : { m_fileButton.data(), m_viewButton.data(), m_goToButton.data(), m_toolsButton.data(), m_selectButton.data() })
    {
        if (button)
        {
            button->setEnabled(isReady);
        }
    }
    if (m_pageField)
    {
        m_pageField->setEnabled(isReady && view->getPageCount() > 0);
    }
    if (m_zoomField)
    {
        m_zoomField->setEnabled(isReady && view->getPageCount() > 0);
    }

    updateSelectButton();
    updatePageField();
    updateZoomField();
}

void PDFAppDocumentToolbar::onViewChanged()
{
    updatePageField();
    updateZoomField();
    updateSelectButton();
}

void PDFAppDocumentToolbar::updateSelectButton()
{
    if (m_selectButton)
    {
        m_selectButton->setIcon(PDFThemeIcons::getIcon(PDFAppDocumentToolbarHelper::getSelectIconName(m_view)));
    }
    if (m_selectMenu && m_selectMenu->isVisible())
    {
        m_selectMenu->updateState();
    }
}

void PDFAppDocumentToolbar::updatePageField()
{
    if (!m_pageField || m_pageField->hasFocus())
    {
        // The text of the user is not replaced while it is typed
        return;
    }

    // The text of the user is replaced: its error goes with it
    m_pageField->setInvalid(false);

    const PDFAppDocumentView* view = m_view.data();
    const PDFInteger pageCount = view ? view->getPageCount() : 0;
    const PDFInteger pageIndex = view ? view->getDisplayedPageIndex() : -1;
    if (pageCount <= 0 || pageIndex < 0)
    {
        m_pageField->setText(QString());
        m_pageField->setSuffix(QString());
        return;
    }

    // page-field 193:847: "308" with the suffix "/ 1592"; a document with page
    // labels shows the label and the number of the page in the suffix ("296 (308 /
    // 1592)" of 1234:164802). The numbers of the field are written without the group
    // separator as the typed numbers (the status item has "1,592")
    QLocale locale = PDFAppLocale::getUiLocale();
    locale.setNumberOptions(locale.numberOptions() | QLocale::OmitGroupSeparator);
    m_pageField->setText(view->getPageLabel(pageIndex));
    if (view->hasPageLabels())
    {
        m_pageField->setSuffix(tr("(%1 / %2)").arg(locale.toString(pageIndex + 1), locale.toString(pageCount)));
    }
    else
    {
        m_pageField->setSuffix(tr("/ %1").arg(locale.toString(pageCount)));
    }
}

void PDFAppDocumentToolbar::updateZoomField()
{
    if (!m_zoomField || m_zoomField->hasFocus())
    {
        return;
    }

    // The text of the user is replaced: its error goes with it
    m_zoomField->setInvalid(false);

    const PDFAppDocumentView* view = m_view.data();
    if (!view || view->getPageCount() <= 0)
    {
        m_zoomField->setText(QString());
        return;
    }

    // zoom-field 193:861: "151 %"
    m_zoomField->setText(tr("%1 %").arg(PDFAppLocale::formatNumber(qRound(view->getZoom() * 100.0))));
}

void PDFAppDocumentToolbar::onPageFieldEdited()
{
    if (!m_pageField || !m_view)
    {
        return;
    }

    const std::optional<PDFInteger> pageIndex = m_view->findPageIndex(m_pageField->text());
    if (!pageIndex)
    {
        // An unknown page keeps the field with the text of the user marked invalid
        m_pageField->setInvalid(true, tr("The document has no page “%1”.").arg(m_pageField->text().trimmed().left(64)));
        m_pageField->selectAll();
        return;
    }

    m_pageField->setInvalid(false);
    m_view->goToPage(*pageIndex);
    m_pageField->clearFocus();
    updatePageField();
}

void PDFAppDocumentToolbar::onZoomFieldEdited()
{
    if (!m_zoomField || !m_view)
    {
        return;
    }

    // "151 %", "151%", "151", "75,5": the language of the user interface first (the
    // text it shows, O17), then the decimal separator of the system (the key of the
    // keyboard) and the C locale
    QString text = m_zoomField->text().trimmed();
    text.remove(QLatin1Char('%'));
    text = text.trimmed();

    bool ok = false;
    double percent = PDFAppLocale::getUiLocale().toDouble(text, &ok);
    if (!ok)
    {
        percent = PDFAppLocale::getRegionalLocale().toDouble(text, &ok);
    }
    if (!ok)
    {
        percent = QLocale::c().toDouble(text, &ok);
    }

    if (!ok || !std::isfinite(percent) || percent <= 0.0)
    {
        m_zoomField->setInvalid(true, tr("Type the zoom in percent, for example 125."));
        m_zoomField->selectAll();
        return;
    }

    m_zoomField->setInvalid(false);
    m_view->setZoom(percent / 100.0);
    m_zoomField->clearFocus();
    updateZoomField();
}

void PDFAppDocumentToolbar::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // bg/subtle with the border border/default at the bottom (toolbar 193:785)
    QPainter painter(this);
    const PDFThemeManager* manager = PDFThemeManager::getInstance();
    painter.fillRect(rect(), manager->getColor(PDFThemeColors::BgSubtle));
    painter.fillRect(QRect(0, height() - 1, width(), 1), manager->getColor(PDFThemeColors::BorderDefault));
}

void PDFAppDocumentToolbar::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    updateCollapse();
}

void PDFAppDocumentToolbar::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    switch (event->type())
    {
        case QEvent::LanguageChange:
            retranslate();
            updateCollapse();
            break;

        case QEvent::FontChange:
        case QEvent::StyleChange:
            updateCollapse();
            break;

        default:
            break;
    }
}

bool PDFAppDocumentToolbar::eventFilter(QObject* watched, QEvent* event)
{
    // A field left with a refused text (Enter has already finished the editing, so
    // QLineEdit does not report the focus out) shows the current value again
    if (event->type() == QEvent::FocusOut && static_cast<const QFocusEvent*>(event)->reason() != Qt::PopupFocusReason)
    {
        if (watched == m_pageField)
        {
            QMetaObject::invokeMethod(this, &PDFAppDocumentToolbar::updatePageField, Qt::QueuedConnection);
        }
        else if (watched == m_zoomField)
        {
            QMetaObject::invokeMethod(this, &PDFAppDocumentToolbar::updateZoomField, Qt::QueuedConnection);
        }
    }

    // Menu bar: an open panel (or the Select menu) grabs the mouse, so the pointer
    // over another panel button is a move of the panel outside of its panel (the
    // margin of its shadow is not a part of it); the panel of that button opens
    if (event->type() == QEvent::MouseMove && isPanelWindow(watched))
    {
        QWidget* popup = static_cast<QWidget*>(watched);
        const QPoint globalPosition = static_cast<const QMouseEvent*>(event)->globalPosition().toPoint();
        const QRect panelRect = popup->rect().marginsRemoved(PDFWidgetStyle::getPopoverMargins(popup));
        if (popup->isVisible() && !panelRect.contains(popup->mapFromGlobal(globalPosition)))
        {
            QToolButton* button = findPanelButtonAt(globalPosition);
            if (button && button != getOpenPanelButton())
            {
                // The closed panel does not handle the move (the toolbar can be gone)
                QPointer<PDFAppDocumentToolbar> guard(this);
                if (switchPanel(button, false) || !guard)
                {
                    return true;
                }
            }
        }
    }

    // Left / Right of the Select menu (a list of tools without submenus): the
    // neighboring panel; the panels ask for it by their signal neighborRequested
    if (event->type() == QEvent::KeyPress && watched == m_selectMenu && m_selectMenu->isVisible())
    {
        const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
        if ((keyEvent->key() == Qt::Key_Left || keyEvent->key() == Qt::Key_Right) && (keyEvent->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier)
        {
            switchToNeighbor((keyEvent->key() == Qt::Key_Right) != (layoutDirection() == Qt::RightToLeft));
            return true;
        }
    }

    return BaseClass::eventFilter(watched, event);
}

QSize PDFAppDocumentToolbar::minimumSizeHint() const
{
    // The toolbar narrows itself, it never makes the window wider
    return QSize(0, HEIGHT);
}

void PDFAppDocumentToolbar::updateCollapse()
{
    if (m_isUpdatingCollapse || !m_layout)
    {
        return;
    }

    m_isUpdatingCollapse = true;

    // The first level, at which the content fits into the width of the toolbar
    int level = PDFAppDocumentToolbarHelper::Full;
    for (; level < PDFAppDocumentToolbarHelper::LastLevel; ++level)
    {
        applyCollapse(level);
        invalidateGroupSizes();
        if (m_layout->sizeHint().width() <= width())
        {
            break;
        }
    }
    applyCollapse(level);
    m_collapseLevel = level;
    m_isUpdatingCollapse = false;
}

void PDFAppDocumentToolbar::invalidateGroupSizes()
{
    // The items of the layout keep the sizes of the groups until the event loop
    // (a group measures itself again only after its LayoutRequest): each level is
    // measured with the sizes of its own buttons, not with those of the previous one
    for (int i = 0; i < m_layout->count(); ++i)
    {
        QLayoutItem* item = m_layout->itemAt(i);
        if (QWidget* group = item ? item->widget() : nullptr)
        {
            if (QLayout* groupLayout = group->layout())
            {
                groupLayout->invalidate();
            }
            group->updateGeometry();
        }
    }
    m_layout->invalidate();
}

void PDFAppDocumentToolbar::applyCollapse(int level)
{
    using H = PDFAppDocumentToolbarHelper;

    for (const QPointer<QToolButton>& button : m_menuButtons)
    {
        if (button)
        {
            button->setToolButtonStyle(level >= H::MenuIconsOnly ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
            button->setIconSize(level >= H::MenuIconsOnly ? QSize(20, 20) : QSize(18, 18));
        }
    }

    if (m_searchField)
    {
        int searchWidth = SEARCH_FIELD_WIDTH;
        if (level >= H::SearchMinimum)
        {
            searchWidth = SEARCH_FIELD_MINIMUM_WIDTH;
        }
        else if (level >= H::SearchNarrow)
        {
            searchWidth = SEARCH_FIELD_NARROW_WIDTH;
        }
        m_searchField->setFixedWidth(searchWidth);
    }

    if (m_zoomGroup)
    {
        m_zoomGroup->setVisible(level < H::ZoomHidden);
    }
    if (m_annotateButton)
    {
        m_annotateButton->setVisible(level < H::AnnotateInsertHidden);
    }
    if (m_insertButton)
    {
        m_insertButton->setVisible(level < H::AnnotateInsertHidden);
    }
    if (m_pageGroup)
    {
        m_pageGroup->setVisible(level < H::PageNavigationHidden);
    }
}

}   // namespace pdf
