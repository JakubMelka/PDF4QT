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

#include "pdfapptooltoolbar.h"

#include "pdfappcommand.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolpresentation.h"

#include "pdfdocumenttoolcatalog.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"
#include "pdfwidgetstyleproperties.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>

#include <algorithm>

namespace pdf
{

/// Helpers of the toolbar of a tool
class PDFAppToolToolbarHelper
{
public:
    PDFAppToolToolbarHelper() = delete;

    /// Property of a button: the id of its command
    static constexpr const char* COMMAND_PROPERTY = "pdf4qtToolCommandId";

    /// Levels of the narrowing of the toolbar
    enum Level
    {
        Full,
        TutorialIconOnly,
        TitleHidden,
        ActionsIconsOnly,
        TutorialHidden,
        UndoRedoHidden,
        ConfirmIconOnly,
        CancelHidden,
        LastLevel = CancelHidden
    };
};

PDFAppToolToolbar::PDFAppToolToolbar(PDFAppToolHost* host, const PDFDocumentToolCatalogEntry& tool, QWidget* parent) :
    BaseClass(parent),
    m_host(host),
    m_toolId(tool.getId())
{
    using C = PDFAppCommands;
    using H = PDFAppToolToolbarHelper;

    setObjectName(QStringLiteral("toolToolbar"));
    setFixedHeight(HEIGHT);

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(PADDING_H, 0, PADDING_H, 0);
    m_layout->setSpacing(GROUP_GAP);

    m_leftGroup = new QWidget(this);
    m_leftGroup->setObjectName(QStringLiteral("toolToolbarLeft"));
    m_leftLayout = new QHBoxLayout(m_leftGroup);
    m_leftLayout->setContentsMargins(0, 0, 0, 0);
    m_leftLayout->setSpacing(LEFT_GAP);

    m_sidebarButton = PDFAppDocumentToolbar::createToolbarButton(C::toggleSidebar(), QStringLiteral("panel-left"), m_leftGroup);
    m_sidebarButton->setProperty(H::COMMAND_PROPERTY, C::toggleSidebar());
    connect(m_sidebarButton.data(), &QToolButton::clicked, this, &PDFAppToolToolbar::onCommandClicked);
    m_leftLayout->addWidget(m_sidebarButton);
    m_leftLayout->addWidget(new PDFAppToolbarDivider(Qt::Vertical, 20, 6, PDFThemeColors::BorderSeparator, m_leftGroup));

    // tool-title 291:1125: paddings 4 / 6, the Tool icon 28, gap 8, Semi Bold 14
    m_titleGroup = new QWidget(m_leftGroup);
    m_titleGroup->setObjectName(QStringLiteral("toolTitle"));
    QHBoxLayout* titleLayout = new QHBoxLayout(m_titleGroup);
    titleLayout->setContentsMargins(4, 0, 6, 0);
    titleLayout->setSpacing(8);
    m_badge = new PDFToolBadge(tool.getIconName(), PDFAppToolPresentation::getHue(tool.getHue()), PDFToolBadge::BadgeSize::Small, m_titleGroup);
    m_badge->setObjectName(QStringLiteral("toolTitleBadge"));
    titleLayout->addWidget(m_badge);
    m_titleLabel = PDFAppPageWidgets::createLabel(tool.getTitle(), QFont::DemiBold, 14, PDFThemeColors::TextPrimary, m_titleGroup);
    m_titleLabel->setObjectName(QStringLiteral("toolTitleName"));
    titleLayout->addWidget(m_titleLabel);
    if (tool.isAdvanced())
    {
        // 391:26688: the Tag Advanced 8 after the title of a tool for experts (as in the
        // catalog of the tools); it says, it does not act
        m_advancedTag = new PDFTag(PDFAppToolPresentation::getAdvancedTagText(), m_titleGroup);
        m_advancedTag->setObjectName(QStringLiteral("toolTitleAdvanced"));
        m_advancedTag->setFocusPolicy(Qt::NoFocus);
        m_advancedTag->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        titleLayout->addWidget(m_advancedTag);
    }
    m_leftLayout->addWidget(m_titleGroup);

    m_undoDivider = new PDFAppToolbarDivider(Qt::Vertical, 20, 6, PDFThemeColors::BorderSeparator, m_leftGroup);
    m_leftLayout->addWidget(m_undoDivider);

    m_undoButton = PDFAppDocumentToolbar::createToolbarButton(C::undo(), QStringLiteral("undo"), m_leftGroup);
    m_undoButton->setProperty(H::COMMAND_PROPERTY, C::undo());
    connect(m_undoButton.data(), &QToolButton::clicked, this, &PDFAppToolToolbar::onCommandClicked);
    m_leftLayout->addWidget(m_undoButton);

    m_redoButton = PDFAppDocumentToolbar::createToolbarButton(C::redo(), QStringLiteral("redo"), m_leftGroup);
    m_redoButton->setProperty(H::COMMAND_PROPERTY, C::redo());
    connect(m_redoButton.data(), &QToolButton::clicked, this, &PDFAppToolToolbar::onCommandClicked);
    m_leftLayout->addWidget(m_redoButton);

    m_actionsDivider = new PDFAppToolbarDivider(Qt::Vertical, 20, 6, PDFThemeColors::BorderSeparator, m_leftGroup);
    m_actionsDivider->hide();
    m_leftLayout->addWidget(m_actionsDivider);

    m_layout->addWidget(m_leftGroup, 0);
    m_layout->addStretch(1);

    m_rightGroup = new QWidget(this);
    m_rightGroup->setObjectName(QStringLiteral("toolToolbarRight"));
    QHBoxLayout* rightLayout = new QHBoxLayout(m_rightGroup);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(RIGHT_GAP);

    // Disabled with its place kept, until the tool has a real video (owner answer Q5)
    m_tutorialButton = PDFAppDocumentToolbar::createToolbarMenuButton(QStringLiteral("toolTutorial"), QStringLiteral("play-circle"), m_rightGroup);
    m_tutorialButton->setCheckable(false);
    m_tutorialButton->setEnabled(false);
    rightLayout->addWidget(m_tutorialButton);

    m_cancelButton = PDFAppPageWidgets::createButton(QString(), QString(), false, m_rightGroup);
    m_cancelButton->setObjectName(QStringLiteral("toolCancel"));
    m_cancelButton->setIcon(QIcon());
    connect(m_cancelButton.data(), &QPushButton::clicked, this, &PDFAppToolToolbar::cancelClicked);
    rightLayout->addWidget(m_cancelButton);

    m_confirmButton = PDFAppPageWidgets::createButton(QString(), tool.getIconName(), true, m_rightGroup);
    m_confirmButton->setObjectName(QStringLiteral("toolConfirm"));
    connect(m_confirmButton.data(), &QPushButton::clicked, this, &PDFAppToolToolbar::confirmClicked);
    rightLayout->addWidget(m_confirmButton);

    m_stopButton = PDFAppPageWidgets::createButton(QString(), QStringLiteral("stop"), false, m_rightGroup);
    m_stopButton->setObjectName(QStringLiteral("toolStop"));
    m_stopButton->hide();
    connect(m_stopButton.data(), &QPushButton::clicked, this, &PDFAppToolToolbar::stopClicked);
    rightLayout->addWidget(m_stopButton);

    m_layout->addWidget(m_rightGroup, 0);

    retranslate();
    updateCommands();

    if (m_host)
    {
        connect(m_host.data(), &PDFAppToolHost::commandStatesChanged, this, &PDFAppToolToolbar::updateCommands);
    }
}

PDFAppToolToolbar::~PDFAppToolToolbar()
{
    for (const PanelItem& item : m_panels)
    {
        if (item.panel)
        {
            item.panel->disconnect(this);
        }
    }
}

void PDFAppToolToolbar::retranslate()
{
    setAccessibleName(tr("Toolbar of the tool"));

    const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(m_toolId);
    if (tool && m_titleLabel)
    {
        m_titleLabel->setText(tool->getTitle());
        m_titleGroup->setToolTip(tool->getTitle());
        m_titleGroup->setAccessibleName(tool->getTitle());
    }
    if (m_advancedTag)
    {
        m_advancedTag->setText(PDFAppToolPresentation::getAdvancedTagText());
    }

    m_tutorialButton->setText(tr("Tutorial"));
    m_tutorialButton->setToolTip(tr("Tutorial (no video yet)"));
    m_tutorialButton->setAccessibleName(tr("Tutorial"));
    m_cancelButton->setText(tr("Cancel"));
    m_stopButton->setText(tr("Stop"));
    m_sidebarButton->setAccessibleName(tr("Sidebar"));
    m_undoButton->setAccessibleName(tr("Undo"));
    m_redoButton->setAccessibleName(tr("Redo"));

    updateCommands();
}

void PDFAppToolToolbar::updateCommands()
{
    if (!m_host)
    {
        return;
    }

    for (QToolButton* button : { m_sidebarButton.data(), m_undoButton.data(), m_redoButton.data() })
    {
        if (button)
        {
            const QString command = button->property(PDFAppToolToolbarHelper::COMMAND_PROPERTY).toString();
            button->setEnabled(m_host->isCommandEnabled(command));
            button->setToolTip(m_host->getCommandToolTip(command));
        }
    }
}

void PDFAppToolToolbar::setToolActions(QWidget* actions)
{
    if (m_actions)
    {
        m_leftLayout->removeWidget(m_actions);
        m_actions->hide();
        m_actions->deleteLater();
    }

    // The panels of the old actions go with them; the new actions have added theirs already
    std::vector<PanelItem> panels;
    for (const PanelItem& item : m_panels)
    {
        if (actions && item.button && actions->isAncestorOf(item.button))
        {
            panels.push_back(item);
        }
    }
    m_panels = std::move(panels);

    m_actions = actions;
    if (actions)
    {
        actions->setParent(m_leftGroup);
        m_leftLayout->addWidget(actions);
        actions->show();

        // Tab goes as the eye: the actions are created after the buttons at the trailing
        // side. The panels below the buttons are other windows.
        QWidget* previous = m_redoButton;
        const QList<QWidget*> children = actions->findChildren<QWidget*>();
        for (QWidget* child : children)
        {
            if (child->window() == window() && (child->focusPolicy() & Qt::TabFocus))
            {
                QWidget::setTabOrder(previous, child);
                previous = child;
            }
        }
    }
    m_actionsDivider->setVisible(actions != nullptr);
    updateCollapse();
}

void PDFAppToolToolbar::setMode(Mode mode)
{
    // After the run (owner 2026-11-09, the after-run frames of Figma, e.g. 296:2761): the
    // buttons keep their places - Cancel Enabled, the Primary button shown and Disabled
    m_mode = mode;
    m_confirmButton->setVisible(mode != Mode::Running);
    m_confirmButton->setEnabled(m_isConfirmEnabled && mode != Mode::Finished);
    m_cancelButton->setEnabled(true);
    m_stopButton->setVisible(mode == Mode::Running);
    updateCollapse();
}

void PDFAppToolToolbar::setConfirmText(const QString& text, const QString& iconName)
{
    if (m_confirmText != text)
    {
        m_confirmText = text;
        m_confirmButton->setAccessibleName(text);
        m_confirmButton->setText(text);
        updateCollapse();
    }
    if (!iconName.isEmpty() && m_confirmButton->property("iconName").toString() != iconName)
    {
        m_confirmButton->setProperty("iconName", iconName);
        m_confirmButton->setIcon(PDFThemeIcons::getIcon(iconName));
    }
}

void PDFAppToolToolbar::setConfirmEnabled(bool isEnabled)
{
    m_isConfirmEnabled = isEnabled;
    m_confirmButton->setEnabled(isEnabled && m_mode != Mode::Finished);
}

void PDFAppToolToolbar::onCommandClicked()
{
    // The button does not identify itself in the signal
    QObject* button = sender();
    if (button && m_host)
    {
        m_host->requestCommand(button->property(PDFAppToolToolbarHelper::COMMAND_PROPERTY).toString());
    }
}


void PDFAppToolToolbar::addPanel(QToolButton* button, PDFAppToolPanelPopup* panel)
{
    if (!button || !panel)
    {
        return;
    }

    PanelItem item;
    item.button = button;
    item.panel = panel;
    m_panels.push_back(item);

    connect(button, &QToolButton::clicked, this, &PDFAppToolToolbar::onPanelButtonClicked);
    connect(panel, &PDFAppDocumentPanelPopup::closed, this, &PDFAppToolToolbar::onPanelClosed);
    connect(panel, &PDFAppDocumentPanelPopup::neighborRequested, this, &PDFAppToolToolbar::onPanelNeighborRequested);
    PDFAppToolbarMenuBar::watchPanel(panel, this);
}

PDFAppToolPanelPopup* PDFAppToolToolbar::openPanel(QToolButton* button)
{
    for (const PanelItem& item : m_panels)
    {
        if (item.button == button && item.panel && button->isVisible() && button->isEnabled())
        {
            // One panel at a time
            QPointer<PDFAppToolToolbar> guard(this);
            QPointer<PDFAppToolPanelPopup> panel = item.panel;
            QPointer<QToolButton> buttonGuard(button);
            if (PDFAppToolPanelPopup* openedPanel = getOpenPanel())
            {
                QToolButton* openedButton = findPanelButton(openedPanel);
                openedPanel->close();
                if (guard && openedButton)
                {
                    openedButton->setChecked(false);
                }
                if (!guard || !buttonGuard || !panel)
                {
                    return nullptr;
                }
            }

            buttonGuard->setChecked(true);
            if (!guard || !buttonGuard || !panel)
            {
                return nullptr;
            }
            panel->popup(buttonGuard, mapToGlobal(QPoint(0, height())).y() + PANEL_GAP);
            if ((!panel || !panel->isVisible()) && buttonGuard)
            {
                buttonGuard->setChecked(false);
            }
            return guard ? panel.data() : nullptr;
        }
    }
    return nullptr;
}

std::vector<QToolButton*> PDFAppToolToolbar::getPanelButtons() const
{
    std::vector<QToolButton*> buttons;
    for (const PanelItem& item : m_panels)
    {
        if (item.button)
        {
            buttons.push_back(item.button);
        }
    }
    return buttons;
}

PDFAppToolPanelPopup* PDFAppToolToolbar::getOpenPanel() const
{
    for (const PanelItem& item : m_panels)
    {
        if (item.panel && item.panel->isVisible())
        {
            return item.panel;
        }
    }
    return nullptr;
}

QToolButton* PDFAppToolToolbar::findPanelButton(const PDFAppToolPanelPopup* panel) const
{
    for (const PanelItem& item : m_panels)
    {
        if (item.panel == panel)
        {
            return item.button;
        }
    }
    return nullptr;
}

bool PDFAppToolToolbar::switchPanel(QToolButton* button)
{
    const PDFAppToolPanelPopup* opened = getOpenPanel();
    if (!opened || !button || button == findPanelButton(opened) || !isVisible() || PDFAppToolbarMenuBar::isBlockedByModalWindow(this))
    {
        return false;
    }
    return openPanel(button) != nullptr;
}

void PDFAppToolToolbar::onPanelButtonClicked()
{
    QToolButton* button = qobject_cast<QToolButton*>(sender());
    if (!button)
    {
        return;
    }

    const PDFAppToolPanelPopup* opened = getOpenPanel();
    if (opened && findPanelButton(opened) == button)
    {
        // A click on the button of the open panel closes it (the popup has done it)
        button->setChecked(false);
        return;
    }
    openPanel(button);
}

void PDFAppToolToolbar::onPanelClosed()
{
    const PDFAppToolPanelPopup* panel = qobject_cast<PDFAppToolPanelPopup*>(sender());
    QToolButton* button = findPanelButton(panel);
    if (button && (!panel || !panel->isVisible()))
    {
        button->setChecked(false);
    }
}

void PDFAppToolToolbar::onPanelNeighborRequested(bool isNext)
{
    if (QToolButton* button = PDFAppToolbarMenuBar::findNeighbor(getPanelButtons(), findPanelButton(getOpenPanel()), isNext))
    {
        switchPanel(button);
    }
}

void PDFAppToolToolbar::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // bg/subtle with the border at the bottom, as the toolbar of the document
    QPainter painter(this);
    const PDFThemeManager* manager = PDFThemeManager::getInstance();
    painter.fillRect(rect(), manager->getColor(PDFThemeColors::BgSubtle));
    painter.fillRect(QRect(0, height() - 1, width(), 1), manager->getColor(PDFThemeColors::BorderDefault));
}

void PDFAppToolToolbar::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    updateCollapse();
}

void PDFAppToolToolbar::changeEvent(QEvent* event)
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

bool PDFAppToolToolbar::eventFilter(QObject* watched, QEvent* event)
{
    // Menu bar: an open panel grabs the mouse, so the pointer over another panel button
    // is a move of the panel outside of its panel
    if (event->type() == QEvent::MouseMove && watched->isWidgetType())
    {
        if (QToolButton* button = PDFAppToolbarMenuBar::findHoverTarget(static_cast<const QWidget*>(watched), event, getPanelButtons(), findPanelButton(getOpenPanel())))
        {
            // The closed panel does not handle the move (the toolbar can be gone)
            QPointer<PDFAppToolToolbar> guard(this);
            if (switchPanel(button) || !guard)
            {
                return true;
            }
        }
    }
    return BaseClass::eventFilter(watched, event);
}

QSize PDFAppToolToolbar::minimumSizeHint() const
{
    // The toolbar narrows itself, it never makes the window wider
    return QSize(0, HEIGHT);
}

void PDFAppToolToolbar::updateCollapse()
{
    using H = PDFAppToolToolbarHelper;

    if (m_isUpdatingCollapse || !m_layout)
    {
        return;
    }

    m_isUpdatingCollapse = true;

    // The first level, at which the content fits into the width of the toolbar
    int level = H::Full;
    for (; level < H::LastLevel; ++level)
    {
        applyCollapse(level);
        invalidateGroupSizes();
        if (m_layout->sizeHint().width() <= width())
        {
            break;
        }
    }
    applyCollapse(level);
    invalidateGroupSizes();
    m_collapseLevel = level;

    m_isUpdatingCollapse = false;
}

void PDFAppToolToolbar::invalidateGroupSizes()
{
    // The items of the layout keep the sizes of the groups until the event loop: each
    // level is measured with the sizes of its own buttons
    for (QWidget* group : { m_leftGroup.data(), m_rightGroup.data(), m_actions.data() })
    {
        if (group)
        {
            if (QLayout* layout = group->layout())
            {
                layout->invalidate();
            }
            group->updateGeometry();
        }
    }
    m_leftLayout->invalidate();
    m_layout->invalidate();
}

void PDFAppToolToolbar::applyCollapse(int level)
{
    using H = PDFAppToolToolbarHelper;

    m_tutorialButton->setToolButtonStyle(level >= H::TutorialIconOnly ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
    m_tutorialButton->setIconSize(level >= H::TutorialIconOnly ? QSize(20, 20) : QSize(18, 18));
    m_tutorialButton->setVisible(level < H::TutorialHidden);
    m_titleLabel->setVisible(level < H::TitleHidden);
    if (m_advancedTag)
    {
        m_advancedTag->setVisible(level < H::TitleHidden);
    }
    m_undoButton->setVisible(level < H::UndoRedoHidden);
    m_redoButton->setVisible(level < H::UndoRedoHidden);

    // Two dividers next to each other without Undo and Redo
    m_undoDivider->setVisible(level < H::UndoRedoHidden || !m_actions);

    // The last steps keep the whole actions (segments of a fixed width): the Primary button
    // says its text by its tooltip and accessible name, Cancel stays as Esc
    m_confirmButton->setText(level >= H::ConfirmIconOnly ? QString() : m_confirmText);
    m_confirmButton->setToolTip(level >= H::ConfirmIconOnly ? m_confirmText : QString());
    m_cancelButton->setVisible(m_mode != Mode::Running && level < H::CancelHidden);

    if (m_actions)
    {
        const QList<QToolButton*> buttons = m_actions->findChildren<QToolButton*>();
        for (QToolButton* button : buttons)
        {
            button->setToolButtonStyle(level >= H::ActionsIconsOnly ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
            button->setIconSize(level >= H::ActionsIconsOnly ? QSize(20, 20) : QSize(18, 18));
        }

        const QList<QWidget*> widgets = m_actions->findChildren<QWidget*>();
        for (QWidget* widget : widgets)
        {
            if (widget->property(NARROW_HIDDEN_PROPERTY).toBool())
            {
                widget->setVisible(level < H::ActionsIconsOnly);
            }
        }
    }
}

}   // namespace pdf

#include "moc_pdfapptooltoolbar.cpp"
